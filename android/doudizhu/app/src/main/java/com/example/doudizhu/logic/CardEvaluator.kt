package com.example.doudizhu.logic

import com.example.doudizhu.model.Card
import com.example.doudizhu.model.CardPattern
import com.example.doudizhu.model.Rank
import com.example.doudizhu.model.groupByRank
import com.example.doudizhu.model.sortedDescending

object CardEvaluator {
    data class PatternResult(
        val pattern: CardPattern,
        val mainValue: Int,
        val length: Int = 0
    )

    fun determinePattern(cards: List<Card>): PatternResult {
        if (cards.isEmpty()) return PatternResult(CardPattern.INVALID, -1)
        val sorted = cards.sortedDescending()
        val grouped = sorted.groupByRank()
        val counts = grouped.values.map { it.size }.sortedDescending()

        return when {
            cards.size == 1 -> PatternResult(CardPattern.SINGLE, sorted.first().weight)
            cards.size == 2 && grouped.size == 1 -> PatternResult(CardPattern.PAIR, sorted.first().weight)
            cards.size == 2 && containsJokerPair(grouped) -> PatternResult(CardPattern.ROCKET, Int.MAX_VALUE)
            cards.size == 3 && counts == listOf(3) -> PatternResult(CardPattern.TRIPLE, sorted.first().weight)
            cards.size == 4 && counts == listOf(4) -> PatternResult(CardPattern.BOMB, sorted.first().weight)
            cards.size == 4 && counts.contains(3) -> PatternResult(CardPattern.TRIPLE_WITH_SINGLE, getRankByCount(grouped, 3))
            cards.size == 5 && counts.contains(3) && counts.contains(2) -> PatternResult(CardPattern.TRIPLE_WITH_PAIR, getRankByCount(grouped, 3))
            isStraight(sorted) -> PatternResult(CardPattern.STRAIGHT, sorted.first().weight, sorted.size)
            isDoubleSequence(grouped) -> PatternResult(CardPattern.DOUBLE_SEQUENCE, getHighestCountRank(grouped, 2), cards.size / 2)
            isAirplane(grouped, cards.size) -> PatternResult(CardPattern.AIRPLANE, getHighestCountRank(grouped, 3), cards.size / 3)
            isAirplaneWithSingles(grouped, cards.size) -> PatternResult(CardPattern.AIRPLANE_WITH_SINGLE, getHighestCountRank(grouped, 3), cards.size / 4)
            isAirplaneWithPairs(grouped, cards.size) -> PatternResult(CardPattern.AIRPLANE_WITH_PAIR, getHighestCountRank(grouped, 3), cards.size / 5)
            isFourWithTwoSingles(grouped) -> PatternResult(CardPattern.FOUR_WITH_TWO_SINGLE, getRankByCount(grouped, 4))
            isFourWithTwoPairs(grouped) -> PatternResult(CardPattern.FOUR_WITH_TWO_PAIR, getRankByCount(grouped, 4))
            else -> PatternResult(CardPattern.INVALID, -1)
        }
    }

    private fun containsJokerPair(grouped: Map<Rank, List<Card>>): Boolean {
        return grouped.keys.containsAll(setOf(Rank.BLACK_JOKER, Rank.RED_JOKER)) && grouped.size == 2
    }

    private fun getRankByCount(grouped: Map<Rank, List<Card>>, count: Int): Int {
        return grouped.entries.firstOrNull { it.value.size == count }?.key?.value ?: -1
    }

    private fun getHighestCountRank(grouped: Map<Rank, List<Card>>, count: Int): Int {
        return grouped.entries.filter { it.value.size >= count }
            .maxByOrNull { it.key.value }?.key?.value ?: -1
    }

    private fun isStraight(sorted: List<Card>): Boolean {
        if (sorted.size < 5) return false
        if (sorted.any { it.rank.value >= Rank.TWO.value }) return false
        val weights = sorted.map { it.weight }.distinct()
        if (weights.size != sorted.size) return false
        return weights.zipWithNext().all { (a, b) -> a - b == 1 }
    }

    private fun isDoubleSequence(grouped: Map<Rank, List<Card>>): Boolean {
        if (grouped.isEmpty()) return false
        if (grouped.values.any { it.size !in setOf(2, 4) }) return false
        val pairs = grouped.filterValues { it.size >= 2 }
        if (pairs.size < 3) return false
        if (pairs.keys.any { it.value >= Rank.TWO.value }) return false
        val values = pairs.keys.map { it.value }.sortedDescending()
        return values.zipWithNext().all { (a, b) -> a - b == 1 }
    }

    private fun isAirplane(grouped: Map<Rank, List<Card>>, total: Int): Boolean {
        val triplets = grouped.filterValues { it.size >= 3 }
        if (triplets.size < 2) return false
        val values = triplets.keys.map { it.value }.sortedDescending()
        if (values.zipWithNext().any { (a, b) -> a - b != 1 }) return false
        val required = triplets.size * 3
        return total == required
    }

    private fun isAirplaneWithSingles(grouped: Map<Rank, List<Card>>, total: Int): Boolean {
        val triplets = grouped.filterValues { it.size == 3 }
        if (triplets.size < 2) return false
        val sequence = triplets.keys.map { it.value }.sortedDescending()
        if (sequence.zipWithNext().any { (a, b) -> a - b != 1 }) return false
        val wings = total - triplets.size * 3
        return wings == triplets.size
    }

    private fun isAirplaneWithPairs(grouped: Map<Rank, List<Card>>, total: Int): Boolean {
        val triplets = grouped.filterValues { it.size == 3 }
        if (triplets.size < 2) return false
        val sequence = triplets.keys.map { it.value }.sortedDescending()
        if (sequence.zipWithNext().any { (a, b) -> a - b != 1 }) return false
        val pairsNeeded = triplets.size
        val pairCount = grouped.filterValues { it.size == 2 }.size
        return total == triplets.size * 3 + pairsNeeded * 2 && pairCount >= pairsNeeded
    }

    private fun isFourWithTwoSingles(grouped: Map<Rank, List<Card>>): Boolean {
        return grouped.values.any { it.size == 4 } && grouped.values.sumOf { it.size } == 6
    }

    private fun isFourWithTwoPairs(grouped: Map<Rank, List<Card>>): Boolean {
        val fourRank = grouped.entries.firstOrNull { it.value.size == 4 }?.key ?: return false
        val pairs = grouped.filter { it.key != fourRank }.values.all { it.size == 2 }
        return pairs && grouped.values.sumOf { it.size } == 8
    }

    fun canBeat(current: PatternResult, previous: PatternResult): Boolean {
        if (current.pattern == CardPattern.INVALID) return false
        if (previous.pattern == CardPattern.INVALID) return true
        if (current.pattern == CardPattern.ROCKET) return true
        if (previous.pattern == CardPattern.ROCKET) return false
        if (current.pattern == CardPattern.BOMB && previous.pattern != CardPattern.BOMB) return true
        if (current.pattern != previous.pattern) return false
        if (current.pattern == CardPattern.BOMB) {
            return current.mainValue > previous.mainValue
        }
        return current.length == previous.length && current.mainValue > previous.mainValue
    }
}
