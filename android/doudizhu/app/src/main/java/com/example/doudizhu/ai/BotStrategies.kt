package com.example.doudizhu.ai

import com.example.doudizhu.logic.CardEvaluator
import com.example.doudizhu.model.Card
import com.example.doudizhu.model.CardPattern
import com.example.doudizhu.model.GameMode
import com.example.doudizhu.model.Player
import com.example.doudizhu.model.TurnAction
import com.example.doudizhu.model.groupByRank
import com.example.doudizhu.model.sortedDescending
import com.example.doudizhu.model.takeLowest

enum class Difficulty {
    CASUAL,
    THINKING
}

class BotStrategies(private val difficulty: Difficulty) {

    fun chooseBid(player: Player, mode: GameMode): Int {
        val highCards = player.hand.count { it.rank.value >= 13 }
        val bombs = player.hand.groupByRank().values.count { it.size == 4 }
        val jokers = player.hand.count { it.rank.value >= 16 }
        val base = when (mode) {
            GameMode.THREE_PLAYER -> 3
            GameMode.FOUR_PLAYER -> 2
        }
        val score = highCards / 2 + bombs * 2 + jokers
        return when {
            score >= 6 -> base
            score >= 3 -> base - 1
            else -> 0
        }.coerceIn(0, base)
    }

    fun choosePlay(
        player: Player,
        previous: TurnAction?
    ): TurnAction {
        val sortedHand = player.hand.sortedDescending()
        val groups = sortedHand.groupByRank()
        val candidates = mutableListOf<TurnAction.Play>()
        sortedHand.forEach { card ->
            candidates += TurnAction.Play(listOf(card), CardEvaluator.determinePattern(listOf(card)).pattern)
        }
        groups.filterValues { it.size >= 2 }.values.forEach { pair ->
            candidates += TurnAction.Play(pair.take(2), CardEvaluator.determinePattern(pair.take(2)).pattern)
        }
        groups.filterValues { it.size >= 3 }.values.forEach { triple ->
            candidates += TurnAction.Play(triple.take(3), CardEvaluator.determinePattern(triple.take(3)).pattern)
        }
        groups.filterValues { it.size == 4 }.values.forEach { bomb ->
            candidates += TurnAction.Play(bomb, CardPattern.BOMB)
        }

        val filtered = if (previous is TurnAction.Play) {
            val prevPattern = CardEvaluator.determinePattern(previous.cards)
            candidates.filter { candidate ->
                val result = CardEvaluator.determinePattern(candidate.cards)
                CardEvaluator.canBeat(result, prevPattern)
            }
        } else {
            candidates
        }

        if (filtered.isEmpty()) return TurnAction.Pass

        return when (difficulty) {
            Difficulty.CASUAL -> filtered.minByOrNull { it.cards.sumOf(Card::weight) } ?: TurnAction.Pass
            Difficulty.THINKING -> selectThinkingMove(filtered, previous)
        }
    }

    private fun selectThinkingMove(
        candidates: List<TurnAction.Play>,
        previous: TurnAction?
    ): TurnAction {
        val sorted = candidates.sortedBy { it.cards.sumOf(Card::weight) }
        if (previous !is TurnAction.Play) {
            return sorted.firstOrNull { it.pattern == CardPattern.SINGLE } ?: sorted.first()
        }
        return sorted.firstOrNull { it.pattern == previous.pattern && it.cards.size == previous.cards.size }
            ?: sorted.first()
    }

    fun chooseHint(player: Player, previous: TurnAction?): List<Card> {
        return when (val action = choosePlay(player, previous)) {
            is TurnAction.Play -> action.cards
            TurnAction.Pass -> player.hand.takeLowest(1)
        }
    }
}
