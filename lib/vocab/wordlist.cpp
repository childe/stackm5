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
abandon       | to leave someone or something behind   | He abandoned the car in the snow.
abundant      | existing in large quantities           | Fish are abundant in this lake.
accumulate    | to gather or build up over time        | Dust accumulated on the old books.
acute         | sharp, severe, or very perceptive      | She felt an acute pain in her knee.
adequate      | enough for a particular purpose        | The room had adequate light to read.
advocate      | to publicly support an idea            | She advocates cycling to work.
aesthetic     | concerned with beauty                  | The building has aesthetic appeal.
ambiguous     | having more than one meaning           | His reply was deliberately ambiguous.
ample         | more than enough                       | There is ample room for everyone.
anticipate    | to expect something to happen          | We anticipate a rise in prices.
arbitrary     | based on whim rather than reason       | The rule seemed arbitrary and unfair.
articulate    | able to express ideas clearly          | She is an articulate speaker.
assess        | to judge the value or quality of       | Teachers assess progress each term.
attribute     | to regard as caused by something       | He attributes his success to luck.
authentic     | real, not a copy                       | The painting proved to be authentic.
benevolent    | kind and generous                      | A benevolent donor paid for it.
bias          | an unfair preference                   | The report showed a clear bias.
candid        | honest and direct                      | She gave a candid account of it.
cease         | to stop happening                      | The rain ceased before dawn.
coherent      | logical and well organized             | She gave a coherent explanation.
collapse      | to fall down suddenly                  | The old bridge collapsed last winter.
compelling    | very convincing or interesting         | He made a compelling argument.
complement    | to make something complete             | The wine complements the fish well.
comply        | to obey a rule or request              | All staff must comply with the rules.
comprehensive | including everything needed            | The guide is comprehensive and clear.
conceal       | to hide something                      | He concealed the letter in a drawer.
concise       | short and clear                        | Please keep your answer concise.
condemn       | to express strong disapproval of       | Leaders condemned the attack.
confine       | to keep within limits                  | Please confine your remarks to facts.
conform       | to behave as others expect             | New members must conform to the rules.
consensus     | general agreement                      | The group reached a consensus quickly.
constrain     | to limit or restrict                   | A small budget constrained the plan.
contemporary  | belonging to the present time          | She studies contemporary art.
contradict    | to say the opposite of                 | His actions contradict his words.
controversial | causing public disagreement            | The film was highly controversial.
conventional  | following accepted custom              | They chose a conventional design.
convey        | to communicate an idea                 | Words cannot convey how I feel.
crucial       | extremely important                    | Timing is crucial in this experiment.
cumulative    | growing by successive additions        | The cumulative effect was serious.
deficient     | lacking something necessary            | The diet was deficient in iron.
deliberate    | done on purpose                        | It was a deliberate act of kindness.
depict        | to show or describe                    | The novel depicts life at sea.
deteriorate   | to become worse                        | His health deteriorated over winter.
deviate       | to move away from a standard           | Do not deviate from the plan.
diminish      | to become smaller or less              | Interest in the topic has diminished.
discrepancy   | a difference between two accounts      | There is a discrepancy in the totals.
distinct      | clearly different or separate          | These are two distinct problems.
diverse       | varied, of many different kinds        | The city has a diverse population.
dubious       | doubtful or suspicious                 | I am dubious about his motives.
elaborate     | detailed and complicated               | She drew an elaborate design.
eliminate     | to remove completely                   | We must eliminate all the errors.
embrace       | to accept eagerly                      | He embraced the new technology.
eminent       | famous and respected                   | An eminent scientist gave the talk.
emphasize     | to give special importance to          | She emphasized the need for care.
endure        | to suffer something patiently          | They endured months of hardship.
enhance       | to improve the quality of              | Music can enhance your mood.
entail        | to involve as a necessary part         | The job entails frequent travel.
erode         | to wear away gradually                 | Waves have eroded the cliff.
evident       | easily seen or understood              | It was evident that he was tired.
exaggerate    | to make seem larger than it is         | Do not exaggerate the danger.
exceed        | to be greater than                     | Costs must not exceed the budget.
explicit      | stated clearly and in detail           | He gave explicit instructions.
exploit       | to use unfairly for your own gain      | Some firms exploit cheap labor.
facilitate    | to make something easier               | A map will facilitate the search.
feasible      | possible to do                         | The plan is not feasible this year.
fluctuate     | to rise and fall irregularly           | Prices fluctuate with the season.
formidable    | causing fear or respect                | She is a formidable opponent.
fundamental   | basic and essential                    | Trust is fundamental to friendship.
hinder        | to make progress difficult             | Bad weather hindered the rescue.
hypothesis    | an idea put forward to be tested       | The data supports our hypothesis.
impartial     | not favoring either side               | A judge must remain impartial.
implement     | to put a plan into action              | We will implement the changes soon.
implicit      | suggested but not stated openly        | There was implicit criticism in it.
incentive     | something that encourages action       | Tax cuts act as an incentive.
inclined      | tending to do something                | I am inclined to agree with you.
indispensable | absolutely necessary                   | A good map is indispensable here.
inevitable    | certain to happen                      | Change is inevitable in any system.
infer         | to conclude from evidence              | We can infer the cause from this.
inherent      | existing as a natural part of          | Risk is inherent in all travel.
initiate      | to begin something                     | They initiated talks last month.
integral      | necessary to make a whole              | Tests are integral to the course.
intricate     | having many small parts                | The lock has an intricate design.
justify       | to show that something is right        | Nothing can justify such cruelty.
legitimate    | lawful or reasonable                   | She has a legitimate complaint.
magnitude     | great size or importance               | We underestimated its magnitude.
mitigate      | to make something less severe          | Trees mitigate the summer heat.
negligible    | too small to be worth considering      | The difference was negligible.
notion        | an idea or belief                      | He had no notion of the danger.
obscure       | not well known, or hard to understand  | The origin of the word is obscure.
paramount     | more important than anything else      | Safety is paramount on this site.
persistent    | continuing despite difficulty          | She has a persistent cough.
plausible     | seeming reasonable or likely           | That sounds like a plausible story.
prevalent     | widespread and common                  | The disease is prevalent in cities.
prominent     | important or easily noticed            | He is a prominent local doctor.
reluctant     | unwilling to do something              | She was reluctant to speak first.
scrutiny      | careful and critical examination       | The plan came under close scrutiny.
subtle        | delicate and not obvious               | There is a subtle difference here.
sufficient    | as much as is needed                   | We have sufficient food for a week.
tentative     | not certain, done as a trial           | We made a tentative arrangement.
viable        | able to work successfully              | This is the only viable option.
)";

}  // namespace vocab
