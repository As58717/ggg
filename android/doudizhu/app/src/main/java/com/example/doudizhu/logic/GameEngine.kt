package com.example.doudizhu.logic

import com.example.doudizhu.model.*
import kotlin.random.Random

class GameEngine(private val random: Random = Random.Default) {
    fun generateDeck(mode: GameMode): MutableList<Card> {
        val deck = mutableListOf<Card>()
        repeat(mode.deckCount) {
            Rank.entries.filter { it != Rank.BLACK_JOKER && it != Rank.RED_JOKER }.forEach { rank ->
                Suit.entries.filter { it != Suit.JOKER }.forEach { suit ->
                    deck += Card(suit, rank)
                }
            }
            deck += Card(Suit.JOKER, Rank.BLACK_JOKER)
            deck += Card(Suit.JOKER, Rank.RED_JOKER)
        }
        deck.shuffle(random)
        return deck
    }

    fun deal(mode: GameMode, deck: MutableList<Card>): Pair<Map<PlayerId, List<Card>>, List<Card>> {
        val players = mutableMapOf<PlayerId, MutableList<Card>>()
        val ids = when (mode.playerCount) {
            3 -> listOf(PlayerId.HUMAN, PlayerId.LEFT_AI, PlayerId.RIGHT_AI)
            else -> listOf(PlayerId.HUMAN, PlayerId.LEFT_AI, PlayerId.RIGHT_AI, PlayerId.TOP_AI)
        }
        ids.forEach { players[it] = mutableListOf() }
        val bottomCardsCount = if (mode.playerCount == 3) 3 else 8
        while (deck.size > bottomCardsCount) {
            ids.forEach { id ->
                if (deck.size > bottomCardsCount) {
                    players[id]?.add(deck.removeAt(0))
                }
            }
        }
        val bottomCards = deck.toList()
        players.forEach { (_, cards) -> cards.sortDescending() }
        return players.mapValues { it.value.toList() } to bottomCards
    }

    fun applyBottomCards(player: Player, bottomCards: List<Card>): Player {
        val updatedHand = (player.hand + bottomCards).sortedDescending()
        return player.copy(hand = updatedHand)
    }

    fun removeCards(player: Player, cards: List<Card>): Player {
        return player.copy(hand = player.hand.withoutCards(cards))
    }

    fun isWin(player: Player): Boolean = player.hand.isEmpty()

    fun calculateMultiplier(base: Int, action: TurnAction): Int {
        return when (action) {
            is TurnAction.Play -> when (action.pattern) {
                CardPattern.BOMB -> base * 2
                CardPattern.ROCKET -> base * 4
                else -> base
            }
            TurnAction.Pass -> base
        }
    }

    fun applySpringMultiplier(
        multiplier: Int,
        landlordWon: Boolean,
        peasantsPlayed: Boolean
    ): Int {
        return when {
            landlordWon && !peasantsPlayed -> multiplier * 2
            !landlordWon && peasantsPlayed.not() -> multiplier * 2
            else -> multiplier
        }
    }
}
