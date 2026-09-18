#include "wordlist.h"

namespace vocab {

/*
 * 100 个考研 / 雅思托福层的单词。
 *
 * 格式： word | definition | example
 *   - 释义必填、例句可省（内置词表里都写了，有测试守着）
 *   - 释义和例句各 ≤ 60 字符 —— 屏幕 30 列、各占 2 行
 *   - 只能用 ASCII：中文引号、长破折号在 AsciiFont8x16 里显示不出来
 *   - # 开头是注释，空行忽略，两侧空格自动去掉（所以可以像下面这样对齐）
 *
 * 加词后记得把 wordlist.h 里的 kExpectedWordCount 一起改，
 * 然后 `pio test -e native` 会把上面这些规则全部对一遍。
 *
 * 注意：释义和例句是 Claude 撰写的，不是词典原文。
 */
const char *kRawWords = R"(
abandon       | /əˈbændən/      | to leave someone or something behind   | He abandoned the car in the snow.
abundant      | /əˈbʌndənt/     | existing in large quantities           | Fish are abundant in this lake.
accumulate    | /əˈkjuːmjəleɪt/ | to gather or build up over time        | Dust accumulated on the old books.
acute         | /əˈkjuːt/       | sharp, severe, or very perceptive      | She felt an acute pain in her knee.
adequate      | /ˈædɪkwət/      | enough for a particular purpose        | The room had adequate light to read.
advocate      | /ˈædvəkeɪt/     | to publicly support an idea            | She advocates cycling to work.
aesthetic     | /ɛsˈθɛtɪk/      | concerned with beauty                  | The building has aesthetic appeal.
ambiguous     | /æmˈbɪgjuəs/    | having more than one meaning           | His reply was deliberately ambiguous.
ample         | /ˈæmpəl/        | more than enough                       | There is ample room for everyone.
anticipate    | /ænˈtɪsəpeɪt/   | to expect something to happen          | We anticipate a rise in prices.
arbitrary     | /ˈɑrbətrɛri/    | based on whim rather than reason       | The rule seemed arbitrary and unfair.
articulate    | /ɑrˈtɪkjələt/   | able to express ideas clearly          | She is an articulate speaker.
assess        | /əˈsɛs/         | to judge the value or quality of       | Teachers assess progress each term.
attribute     | /əˈtrɪbjut/     | to regard as caused by something       | He attributes his success to luck.
authentic     | /ɔˈθɛntɪk/      | real, not a copy                       | The painting proved to be authentic.
benevolent    | /bəˈnɛvələnt/   | kind and generous                      | A benevolent donor paid for it.
bias          | /ˈbaɪəs/        | an unfair preference                   | The report showed a clear bias.
candid        | /ˈkændɪd/       | honest and direct                      | She gave a candid account of it.
cease         | /siːs/          | to stop happening                      | The rain ceased before dawn.
coherent      | /koʊˈhɪrənt/    | logical and well organized             | She gave a coherent explanation.
collapse      | /kəˈlæps/       | to fall down suddenly                  | The old bridge collapsed last winter.
compelling    | /kəmˈpɛlɪŋ/     | very convincing or interesting         | He made a compelling argument.
complement    | /ˈkɑmpləmɛnt/   | to make something complete             | The wine complements the fish well.
comply        | /kəmˈplaɪ/      | to obey a rule or request              | All staff must comply with the rules.
comprehensive | /kɑmprɪˈhɛnsɪv/ | including everything needed            | The guide is comprehensive and clear.
conceal       | /kənˈsiːl/      | to hide something                      | He concealed the letter in a drawer.
concise       | /kənˈsaɪs/      | short and clear                        | Please keep your answer concise.
condemn       | /kənˈdɛm/       | to express strong disapproval of       | Leaders condemned the attack.
confine       | /kənˈfaɪn/      | to keep within limits                  | Please confine your remarks to facts.
conform       | /kənˈfɔrm/      | to behave as others expect             | New members must conform to the rules.
consensus     | /kənˈsɛnsəs/    | general agreement                      | The group reached a consensus quickly.
constrain     | /kənˈstreɪn/    | to limit or restrict                   | A small budget constrained the plan.
contemporary  | /kənˈtɛmpərɛri/ | belonging to the present time          | She studies contemporary art.
contradict    | /kɑntrəˈdɪkt/   | to say the opposite of                 | His actions contradict his words.
controversial | /kɑntrəˈvɜrʃəl/ | causing public disagreement            | The film was highly controversial.
conventional  | /kənˈvɛnʃənəl/  | following accepted custom              | They chose a conventional design.
convey        | /kənˈveɪ/       | to communicate an idea                 | Words cannot convey how I feel.
crucial       | /ˈkruːʃəl/      | extremely important                    | Timing is crucial in this experiment.
cumulative    | /ˈkjuːmjələtɪv/ | growing by successive additions        | The cumulative effect was serious.
deficient     | /dɪˈfɪʃənt/     | lacking something necessary            | The diet was deficient in iron.
deliberate    | /dɪˈlɪbərət/    | done on purpose                        | It was a deliberate act of kindness.
depict        | /dɪˈpɪkt/       | to show or describe                    | The novel depicts life at sea.
deteriorate   | /dɪˈtɪriəreɪt/  | to become worse                        | His health deteriorated over winter.
deviate       | /ˈdiːvieɪt/     | to move away from a standard           | Do not deviate from the plan.
diminish      | /dɪˈmɪnɪʃ/      | to become smaller or less              | Interest in the topic has diminished.
discrepancy   | /dɪˈskrɛpənsi/  | a difference between two accounts      | There is a discrepancy in the totals.
distinct      | /dɪˈstɪŋkt/     | clearly different or separate          | These are two distinct problems.
diverse       | /daɪˈvɜrs/      | varied, of many different kinds        | The city has a diverse population.
dubious       | /ˈduːbiəs/      | doubtful or suspicious                 | I am dubious about his motives.
elaborate     | /ɪˈlæbərət/     | detailed and complicated               | She drew an elaborate design.
eliminate     | /ɪˈlɪməneɪt/    | to remove completely                   | We must eliminate all the errors.
embrace       | /ɪmˈbreɪs/      | to accept eagerly                      | He embraced the new technology.
eminent       | /ˈɛmɪnənt/      | famous and respected                   | An eminent scientist gave the talk.
emphasize     | /ˈɛmfəsaɪz/     | to give special importance to          | She emphasized the need for care.
endure        | /ɪnˈdʊr/        | to suffer something patiently          | They endured months of hardship.
enhance       | /ɪnˈhæns/       | to improve the quality of              | Music can enhance your mood.
entail        | /ɪnˈteɪl/       | to involve as a necessary part         | The job entails frequent travel.
erode         | /ɪˈroʊd/        | to wear away gradually                 | Waves have eroded the cliff.
evident       | /ˈɛvɪdənt/      | easily seen or understood              | It was evident that he was tired.
exaggerate    | /ɪgˈzædʒəreɪt/  | to make seem larger than it is         | Do not exaggerate the danger.
exceed        | /ɪkˈsiːd/       | to be greater than                     | Costs must not exceed the budget.
explicit      | /ɪkˈsplɪsɪt/    | stated clearly and in detail           | He gave explicit instructions.
exploit       | /ɪkˈsplɔɪt/     | to use unfairly for your own gain      | Some firms exploit cheap labor.
facilitate    | /fəˈsɪləteɪt/   | to make something easier               | A map will facilitate the search.
feasible      | /ˈfiːzəbəl/     | possible to do                         | The plan is not feasible this year.
fluctuate     | /ˈflʌktʃueɪt/   | to rise and fall irregularly           | Prices fluctuate with the season.
formidable    | /ˈfɔrmɪdəbəl/   | causing fear or respect                | She is a formidable opponent.
fundamental   | /fʌndəˈmɛntəl/  | basic and essential                    | Trust is fundamental to friendship.
hinder        | /ˈhɪndər/       | to make progress difficult             | Bad weather hindered the rescue.
hypothesis    | /haɪˈpɑθəsɪs/   | an idea put forward to be tested       | The data supports our hypothesis.
impartial     | /ɪmˈpɑrʃəl/     | not favoring either side               | A judge must remain impartial.
implement     | /ˈɪmpləmɛnt/    | to put a plan into action              | We will implement the changes soon.
implicit      | /ɪmˈplɪsɪt/     | suggested but not stated openly        | There was implicit criticism in it.
incentive     | /ɪnˈsɛntɪv/     | something that encourages action       | Tax cuts act as an incentive.
inclined      | /ɪnˈklaɪnd/     | tending to do something                | I am inclined to agree with you.
indispensable | /ɪndɪˈspɛnsəbəl/| absolutely necessary                   | A good map is indispensable here.
inevitable    | /ɪnˈɛvɪtəbəl/   | certain to happen                      | Change is inevitable in any system.
infer         | /ɪnˈfɜr/        | to conclude from evidence              | We can infer the cause from this.
inherent      | /ɪnˈhɪrənt/     | existing as a natural part of          | Risk is inherent in all travel.
initiate      | /ɪˈnɪʃieɪt/     | to begin something                     | They initiated talks last month.
integral      | /ˈɪntɪgrəl/     | necessary to make a whole              | Tests are integral to the course.
intricate     | /ˈɪntrɪkət/     | having many small parts                | The lock has an intricate design.
justify       | /ˈdʒʌstɪfaɪ/    | to show that something is right        | Nothing can justify such cruelty.
legitimate    | /lɪˈdʒɪtəmət/   | lawful or reasonable                   | She has a legitimate complaint.
magnitude     | /ˈmægnɪtuːd/    | great size or importance               | We underestimated its magnitude.
mitigate      | /ˈmɪtɪgeɪt/     | to make something less severe          | Trees mitigate the summer heat.
negligible    | /ˈnɛglɪdʒəbəl/  | too small to be worth considering      | The difference was negligible.
notion        | /ˈnoʊʃən/       | an idea or belief                      | He had no notion of the danger.
obscure       | /əbˈskjʊr/      | not well known, or hard to understand  | The origin of the word is obscure.
paramount     | /ˈpærəmaʊnt/    | more important than anything else      | Safety is paramount on this site.
persistent    | /pərˈsɪstənt/   | continuing despite difficulty          | She has a persistent cough.
plausible     | /ˈplɔzəbəl/     | seeming reasonable or likely           | That sounds like a plausible story.
prevalent     | /ˈprɛvələnt/    | widespread and common                  | The disease is prevalent in cities.
prominent     | /ˈprɑmɪnənt/    | important or easily noticed            | He is a prominent local doctor.
reluctant     | /rɪˈlʌktənt/    | unwilling to do something              | She was reluctant to speak first.
scrutiny      | /ˈskruːtəni/    | careful and critical examination       | The plan came under close scrutiny.
subtle        | /ˈsʌtəl/        | delicate and not obvious               | There is a subtle difference here.
sufficient    | /səˈfɪʃənt/     | as much as is needed                   | We have sufficient food for a week.
tentative     | /ˈtɛntətɪv/     | not certain, done as a trial           | We made a tentative arrangement.
viable        | /ˈvaɪəbəl/      | able to work successfully              | This is the only viable option.
)";

}  // namespace vocab
