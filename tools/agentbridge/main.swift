// agentbridge：在 Unix socket 和 Cardputer 的 AGENTS 蓝牙服务之间逐行转发。
//
// Hammerspoon 连 ~/.cache/agentbridge.sock，发来的每一行原样写给 Cardputer（一行 = 一次
// write-with-response，按顺序一条一条发）；Cardputer notify 来的每一条加 \n 写回 socket。
// 另外报告蓝牙连接状态：C<tab>1 / C<tab>0，新客户端连上时立即补发一次。
// 不解析内容，协议见 docs/superpowers/specs/2026-11-14-agents-app-design.md。
//
// 为什么是单独的进程：Hammerspoon.app 没有蓝牙用途说明，它起的子进程一碰蓝牙就被系统杀掉。
// 这个二进制把带 NSBluetoothAlwaysUsageDescription 的 Info.plist 嵌在 __TEXT,__info_plist 里，
// 由 launchd 启动，TCC 认它自己。

import CoreBluetooth
import Foundation

let serviceUUID = CBUUID(string: "a7c4f3e0-5b1d-4e8a-9c2f-1d3b6e8a0001")
let writeUUID = CBUUID(string: "a7c4f3e0-5b1d-4e8a-9c2f-1d3b6e8a0002")
let notifyUUID = CBUUID(string: "a7c4f3e0-5b1d-4e8a-9c2f-1d3b6e8a0003")
let socketPath = NSHomeDirectory() + "/.cache/agentbridge.sock"

// 所有状态只在这一个串行队列上改：CoreBluetooth 回调和 socket 事件都派发到这里
let queue = DispatchQueue(label: "agentbridge")

func log(_ message: String) {
    let stamp = ISO8601DateFormatter().string(from: Date())
    FileHandle.standardError.write("\(stamp) \(message)\n".data(using: .utf8)!)
}

final class Bridge: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var writeChr: CBCharacteristic?
    private var linked = false

    // 待写给 Cardputer 的消息；上一条收到 didWriteValueFor 才发下一条
    private var pending: [Data] = []
    private var writing = false

    private var listenFD: Int32 = -1
    private var listenSource: DispatchSourceRead?
    private var clientFD: Int32 = -1
    private var clientSource: DispatchSourceRead?
    private var inBuffer = Data()

    func start() {
        signal(SIGPIPE, SIG_IGN)
        listen()
        central = CBCentralManager(delegate: self, queue: queue)
    }

    // MARK: - 蓝牙

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        log("bluetooth state \(central.state.rawValue)")
        if central.state == .poweredOn {
            scan()
        } else {
            dropPeripheral()
        }
    }

    private func scan() {
        guard central.state == .poweredOn, !central.isScanning else { return }
        log("scanning")
        central.scanForPeripherals(withServices: [serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didDiscover p: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        guard peripheral == nil else { return }
        log("found \(p.identifier), connecting")
        central.stopScan()
        peripheral = p
        p.delegate = self
        central.connect(p)
    }

    func centralManager(_ central: CBCentralManager, didConnect p: CBPeripheral) {
        p.discoverServices([serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        log("connect failed: \(error?.localizedDescription ?? "-")")
        dropPeripheral()
        scan()
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral p: CBPeripheral,
                        error: Error?) {
        log("disconnected: \(error?.localizedDescription ?? "-")")
        dropPeripheral()
        scan()
    }

    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        guard let service = p.services?.first(where: { $0.uuid == serviceUUID }) else {
            log("AGENTS service missing")
            central.cancelPeripheralConnection(p)
            return
        }
        p.discoverCharacteristics([writeUUID, notifyUUID], for: service)
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        let chars = service.characteristics ?? []
        writeChr = chars.first(where: { $0.uuid == writeUUID })
        guard writeChr != nil, let n = chars.first(where: { $0.uuid == notifyUUID }) else {
            log("AGENTS characteristics missing")
            central.cancelPeripheralConnection(p)
            return
        }
        p.setNotifyValue(true, for: n)
    }

    func peripheral(_ p: CBPeripheral, didUpdateNotificationStateFor c: CBCharacteristic, error: Error?) {
        if c.isNotifying && writeChr != nil {
            log("linked")
            setLinked(true)
        }
    }

    func peripheral(_ p: CBPeripheral, didUpdateValueFor c: CBCharacteristic, error: Error?) {
        guard c.uuid == notifyUUID, let value = c.value else { return }
        sendToClient(value + Data([0x0A]))
    }

    func peripheral(_ p: CBPeripheral, didWriteValueFor c: CBCharacteristic, error: Error?) {
        if let error { log("write failed: \(error.localizedDescription)") }
        writing = false
        pump()
    }

    private func dropPeripheral() {
        peripheral = nil
        writeChr = nil
        setLinked(false)
    }

    private func setLinked(_ on: Bool) {
        if !on {
            pending.removeAll()
            writing = false
        }
        guard linked != on else { return }
        linked = on
        sendStatus()
    }

    private func pump() {
        guard !writing, !pending.isEmpty, let p = peripheral, let w = writeChr else { return }
        writing = true
        p.writeValue(pending.removeFirst(), for: w, type: .withResponse)
    }

    // MARK: - Unix socket

    private func listen() {
        unlink(socketPath)
        listenFD = socket(AF_UNIX, SOCK_STREAM, 0)
        guard listenFD >= 0 else { fatalError("socket: \(errno)") }

        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        let capacity = MemoryLayout.size(ofValue: addr.sun_path)
        guard socketPath.utf8.count < capacity else { fatalError("socket path too long") }
        withUnsafeMutablePointer(to: &addr.sun_path) { ptr in
            ptr.withMemoryRebound(to: CChar.self, capacity: capacity) { _ = strcpy($0, socketPath) }
        }
        let ok = withUnsafePointer(to: &addr) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                bind(listenFD, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
        guard ok == 0, Darwin.listen(listenFD, 4) == 0 else { fatalError("bind/listen: \(errno)") }
        log("listening on \(socketPath)")

        let source = DispatchSource.makeReadSource(fileDescriptor: listenFD, queue: queue)
        source.setEventHandler { [weak self] in self?.accept() }
        source.resume()
        listenSource = source
    }

    private func accept() {
        let fd = Darwin.accept(listenFD, nil, nil)
        guard fd >= 0 else { return }
        closeClient()  // 同一时刻只服务一个客户端，新的顶掉旧的
        log("client connected")
        clientFD = fd
        inBuffer.removeAll()

        let source = DispatchSource.makeReadSource(fileDescriptor: fd, queue: queue)
        source.setEventHandler { [weak self] in self?.readClient() }
        source.setCancelHandler { close(fd) }  // 文档要求 fd 在 cancel 之后才关
        source.resume()
        clientSource = source
        sendStatus()
    }

    private func readClient() {
        var buf = [UInt8](repeating: 0, count: 4096)
        let n = read(clientFD, &buf, buf.count)
        if n <= 0 {
            log("client disconnected")
            closeClient()
            return
        }
        inBuffer.append(contentsOf: buf[0..<n])
        // 按 \n 切行，不完整的尾巴留到下次
        while let nl = inBuffer.firstIndex(of: 0x0A) {
            let line = inBuffer[inBuffer.startIndex..<nl]
            inBuffer.removeSubrange(inBuffer.startIndex...nl)
            if linked && !line.isEmpty {
                pending.append(Data(line))
            }
        }
        pump()
    }

    private func closeClient() {
        clientSource?.cancel()
        clientSource = nil
        clientFD = -1
    }

    private func sendStatus() {
        sendToClient((linked ? "C\t1\n" : "C\t0\n").data(using: .utf8)!)
    }

    private func sendToClient(_ data: Data) {
        guard clientFD >= 0 else { return }
        let written = data.withUnsafeBytes { write(clientFD, $0.baseAddress, data.count) }
        if written < 0 {
            log("client write failed: \(errno)")
            closeClient()
        }
    }
}

let bridge = Bridge()
queue.async { bridge.start() }
dispatchMain()
