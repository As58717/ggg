package com.example.doudizhu.model

import kotlin.math.min

enum class Suit { SPADE, HEART, CLUB, DIAMOND, JOKER }

enum class Rank(val value: Int) {
    THREE(3),
    FOUR(4),
    FIVE(5),
    SIX(6),
    SEVEN(7),
    EIGHT(8),
    NINE(9),
    TEN(10),
    JACK(11),
    QUEEN(12),
    KING(13),
    ACE(14),
    TWO(15),
    BLACK_JOKER(16),
    RED_JOKER(17);

    companion object {
        fun fromValue(value: Int): Rank = entries.first { it.value == value }
    }
}

data class Card(
    val suit: Suit,
    val rank: Rank
) : Comparable<Card> {
    val weight: Int = when (rank) {
        Rank.BLACK_JOKER -> 16
        Rank.RED_JOKER -> 17
        else -> rank.value
    }

    override fun compareTo(other: Card): Int = weight.compareTo(other.weight)

    val displayName: String
        get() = when (rank) {
            Rank.BLACK_JOKER -> "JOKER"
            Rank.RED_JOKER -> "JOKER"
            else -> when (rank.value) {
                in 3..10 -> rank.value.toString()
                11 -> "J"
                12 -> "Q"
                13 -> "K"
                14 -> "A"
                15 -> "2"
                else -> rank.name
            }
        }
}

enum class PlayerId { HUMAN, LEFT_AI, RIGHT_AI, TOP_AI }

data class Player(
    val id: PlayerId,
    val name: String,
    val isHuman: Boolean,
    val hand: List<Card> = emptyList(),
    val score: Int = 0,
    val isLandlord: Boolean = false
)

enum class GameMode(val playerCount: Int, val deckCount: Int) {
    THREE_PLAYER(3, 1),
    FOUR_PLAYER(4, 2)
}

sealed class TurnAction {
    data object Pass : TurnAction()
    data class Play(val cards: List<Card>, val pattern: CardPattern) : TurnAction()
}

enum class CardPattern(val power: Int) {
    INVALID(-1),
    SINGLE(1),
    PAIR(2),
    TRIPLE(3),
    TRIPLE_WITH_SINGLE(4),
    TRIPLE_WITH_PAIR(5),
    STRAIGHT(6),
    DOUBLE_SEQUENCE(7),
    AIRPLANE(8),
    AIRPLANE_WITH_SINGLE(9),
    AIRPLANE_WITH_PAIR(10),
    FOUR_WITH_TWO_SINGLE(11),
    FOUR_WITH_TWO_PAIR(12),
    BOMB(13),
    ROCKET(14)
}

sealed class GamePhase {
    data object Idle : GamePhase()
    data object Shuffling : GamePhase()
    data object Dealing : GamePhase()
    data class Bidding(val currentBid: Int) : GamePhase()
    data class Robbing(val currentBid: Int) : GamePhase()
    data class Playing(val currentPlayer: PlayerId, val lastAction: TurnAction?) : GamePhase()
    data class Settled(val landlord: PlayerId, val winner: PlayerId, val multiplier: Int) : GamePhase()
}

sealed class GameEvent {
    data object Start : GameEvent()
    data class Bid(val playerId: PlayerId, val score: Int) : GameEvent()
    data class Rob(val playerId: PlayerId, val score: Int) : GameEvent()
    data class DecideLandlord(val playerId: PlayerId) : GameEvent()
    data class PlayCards(val playerId: PlayerId, val action: TurnAction) : GameEvent()
    data object Finish : GameEvent()
}

data class GameUiState(
    val mode: GameMode = GameMode.THREE_PLAYER,
    val players: Map<PlayerId, Player> = emptyMap(),
    val landlord: PlayerId? = null,
    val bottomCards: List<Card> = emptyList(),
    val phase: GamePhase = GamePhase.Idle,
    val multiplier: Int = 1,
    val remainingCards: Map<PlayerId, Int> = emptyMap(),
    val lastPlayed: Map<PlayerId, TurnAction> = emptyMap(),
    val hintSelection: List<Card> = emptyList(),
    val audioCue: AudioCue? = null
)

enum class AudioCue {
    SHUFFLE,
    DEAL,
    PLAY,
    PASS,
    WIN,
    LOSE
}

fun List<Card>.toFriendlyString(): String = joinToString { it.displayName }

fun List<Card>.withoutCards(cards: Collection<Card>): List<Card> {
    val mutable = toMutableList()
    cards.forEach { card ->
        val index = mutable.indexOfFirst { it.rank == card.rank && it.suit == card.suit }
        if (index >= 0) {
            mutable.removeAt(index)
        }
    }
    return mutable
}

fun Collection<Card>.groupByRank(): Map<Rank, List<Card>> = groupBy { it.rank }

fun List<Card>.sortedDescending(): List<Card> = sortedByDescending { it.weight }

fun Collection<Card>.takeLowest(count: Int): List<Card> = this.sortedBy { it.weight }.take(min(count, size))
