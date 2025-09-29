package com.example.doudizhu.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.example.doudizhu.ai.BotStrategies
import com.example.doudizhu.ai.Difficulty
import com.example.doudizhu.logic.CardEvaluator
import com.example.doudizhu.logic.GameEngine
import com.example.doudizhu.model.AudioCue
import com.example.doudizhu.model.Card
import com.example.doudizhu.model.CardPattern
import com.example.doudizhu.model.GameEvent
import com.example.doudizhu.model.GameMode
import com.example.doudizhu.model.GamePhase
import com.example.doudizhu.model.GameUiState
import com.example.doudizhu.model.Player
import com.example.doudizhu.model.PlayerId
import com.example.doudizhu.model.TurnAction
import com.example.doudizhu.model.withoutCards
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

class GameViewModel(
    private val engine: GameEngine = GameEngine(),
    private var botDifficulty: Difficulty = Difficulty.CASUAL
) : ViewModel() {

    private val _state = MutableStateFlow(GameUiState())
    val state: StateFlow<GameUiState> = _state

    private val bots = mapOf(
        Difficulty.CASUAL to BotStrategies(Difficulty.CASUAL),
        Difficulty.THINKING to BotStrategies(Difficulty.THINKING)
    )

    fun startGame(mode: GameMode = _state.value.mode) {
        viewModelScope.launch {
            updateState { it.copy(mode = mode, phase = GamePhase.Shuffling, audioCue = AudioCue.SHUFFLE) }
            delay(250)
            val deck = engine.generateDeck(mode)
            updateState { it.copy(phase = GamePhase.Dealing, audioCue = AudioCue.DEAL) }
            delay(250)
            val (hands, bottom) = engine.deal(mode, deck)
            val players = hands.map { (id, cards) ->
                Player(
                    id = id,
                    name = when (id) {
                        PlayerId.HUMAN -> "你"
                        PlayerId.LEFT_AI -> "左家"
                        PlayerId.RIGHT_AI -> if (mode.playerCount == 3) "右家" else "右家"
                        PlayerId.TOP_AI -> "上家"
                    },
                    isHuman = id == PlayerId.HUMAN,
                    hand = cards.sortedByDescending { it.weight }
                )
            }.associateBy { it.id }

            updateState {
                it.copy(
                    players = players,
                    bottomCards = bottom,
                    phase = GamePhase.Bidding(currentBid = 0),
                    landlord = null,
                    lastPlayed = emptyMap(),
                    remainingCards = players.mapValues { entry -> entry.value.hand.size }
                )
            }
            handleAIBidding()
        }
    }

    fun onEvent(event: GameEvent) {
        when (event) {
            is GameEvent.Start -> startGame(_state.value.mode)
            is GameEvent.Bid -> handleBid(event.playerId, event.score)
            is GameEvent.Rob -> handleRob(event.playerId, event.score)
            is GameEvent.DecideLandlord -> decideLandlord(event.playerId)
            is GameEvent.PlayCards -> handlePlay(event.playerId, event.action)
            GameEvent.Finish -> finishGame()
        }
    }

    fun updateDifficulty(difficulty: Difficulty) {
        botDifficulty = difficulty
    }

    private fun handleBid(playerId: PlayerId, bid: Int) {
        val currentPhase = _state.value.phase
        if (currentPhase !is GamePhase.Bidding) return
        val newBid = maxOf(currentPhase.currentBid, bid)
        val nextPhase = if (bid == 0) currentPhase else GamePhase.Robbing(newBid)
        updateState { state ->
            state.copy(
                phase = nextPhase,
                multiplier = maxOf(state.multiplier, newBid.coerceAtLeast(1))
            )
        }
        if (playerId == PlayerId.HUMAN && bid > 0) {
            launchAIAfterDelay { decideLandlord(playerId) }
        }
    }

    private fun handleRob(playerId: PlayerId, score: Int) {
        val currentPhase = _state.value.phase
        if (currentPhase !is GamePhase.Robbing) return
        val finalLandlord = if (score >= currentPhase.currentBid) playerId else PlayerId.HUMAN
        decideLandlord(finalLandlord)
    }

    private fun decideLandlord(playerId: PlayerId) {
        val state = _state.value
        val players = state.players.toMutableMap()
        val landlord = engine.applyBottomCards(players.getValue(playerId), state.bottomCards)
        players[playerId] = landlord.copy(isLandlord = true)
        val updated = players.mapValues { (id, player) ->
            if (id == playerId) landlord.copy(isLandlord = true) else player.copy(isLandlord = false)
        }
        updateState {
            it.copy(
                players = updated,
                landlord = playerId,
                phase = GamePhase.Playing(playerId, null),
                multiplier = it.multiplier.coerceAtLeast(1),
                remainingCards = updated.mapValues { entry -> entry.value.hand.size }
            )
        }
        if (playerId != PlayerId.HUMAN) {
            launchAIAfterDelay { aiPlayTurn(playerId) }
        }
    }

    private fun handlePlay(playerId: PlayerId, action: TurnAction) {
        val currentPhase = _state.value.phase
        if (currentPhase !is GamePhase.Playing) return
        val players = _state.value.players.toMutableMap()
        val player = players[playerId] ?: return
        when (action) {
            is TurnAction.Play -> {
                val pattern = CardEvaluator.determinePattern(action.cards)
                if (pattern.pattern == CardPattern.INVALID) return
                val previous = currentPhase.lastAction
                if (previous is TurnAction.Play) {
                    val prevResult = CardEvaluator.determinePattern(previous.cards)
                    if (!CardEvaluator.canBeat(pattern, prevResult)) return
                }
                players[playerId] = player.copy(hand = player.hand.withoutCards(action.cards))
                val nextMultiplier = engine.calculateMultiplier(_state.value.multiplier, TurnAction.Play(action.cards, pattern.pattern))
                val nextLastPlayed = _state.value.lastPlayed.toMutableMap()
                nextLastPlayed[playerId] = TurnAction.Play(action.cards, pattern.pattern)
                val nextPlayer = nextPlayerId(playerId)
                updateState {
                    it.copy(
                        players = players,
                        phase = GamePhase.Playing(nextPlayer, TurnAction.Play(action.cards, pattern.pattern)),
                        lastPlayed = nextLastPlayed,
                        multiplier = nextMultiplier,
                        remainingCards = players.mapValues { entry -> entry.value.hand.size },
                        audioCue = AudioCue.PLAY,
                        hintSelection = emptyList()
                    )
                }
                checkForWin(playerId)
            }
            TurnAction.Pass -> {
                val nextPlayer = nextPlayerId(playerId)
                val nextLast = if (nextPlayer == currentPhase.lastActionPlayer(playerId)) null else currentPhase.lastAction
                updateState {
                    it.copy(
                        phase = GamePhase.Playing(nextPlayer, nextLast),
                        lastPlayed = it.lastPlayed + (playerId to TurnAction.Pass),
                        audioCue = AudioCue.PASS,
                        hintSelection = emptyList()
                    )
                }
            }
        }
        val nextPlayer = (_state.value.phase as? GamePhase.Playing)?.currentPlayer ?: return
        if (nextPlayer != PlayerId.HUMAN) {
            launchAIAfterDelay { aiPlayTurn(nextPlayer) }
        }
    }

    private fun GamePhase.Playing.lastActionPlayer(current: PlayerId): PlayerId? {
        val actionEntry = _state.value.lastPlayed.entries.findLast { it.key != current && it.value is TurnAction.Play }
        return actionEntry?.key
    }

    private fun nextPlayerId(current: PlayerId): PlayerId {
        val order = when (_state.value.mode.playerCount) {
            3 -> listOf(PlayerId.HUMAN, PlayerId.LEFT_AI, PlayerId.RIGHT_AI)
            else -> listOf(PlayerId.HUMAN, PlayerId.LEFT_AI, PlayerId.TOP_AI, PlayerId.RIGHT_AI)
        }
        val index = order.indexOf(current)
        return order[(index + 1) % order.size]
    }

    private fun checkForWin(playerId: PlayerId) {
        val player = _state.value.players[playerId] ?: return
        if (player.hand.isEmpty()) {
            val landlordWon = player.isLandlord
            val landlord = _state.value.landlord ?: playerId
            val multiplier = engine.applySpringMultiplier(
                _state.value.multiplier,
                landlordWon,
                peasantsPlayed = _state.value.lastPlayed.any { (id, action) ->
                    action is TurnAction.Play && id != landlord
                }
            )
            updateState {
                it.copy(
                    phase = GamePhase.Settled(
                        landlord = landlord,
                        winner = playerId,
                        multiplier = multiplier
                    ),
                    audioCue = if (playerId == PlayerId.HUMAN) AudioCue.WIN else AudioCue.LOSE
                )
            }
        }
    }

    private fun finishGame() {
        updateState { GameUiState(mode = it.mode) }
    }

    private fun handleAIBidding() {
        viewModelScope.launch {
            val mode = _state.value.mode
            val players = _state.value.players.values.filter { !it.isHuman }
            players.forEach { player ->
                delay(500)
                val bid = bots.getValue(botDifficulty).chooseBid(player, mode)
                if (bid > 0) {
                    decideLandlord(player.id)
                    return@launch
                }
            }
            decideLandlord(PlayerId.HUMAN)
        }
    }

    private fun aiPlayTurn(playerId: PlayerId) {
        val state = _state.value
        val player = state.players[playerId] ?: return
        val previous = (state.phase as? GamePhase.Playing)?.lastAction
        val action = bots.getValue(botDifficulty).choosePlay(player, previous)
        handlePlay(playerId, action)
    }

    fun humanPlaySelected(cards: List<Card>) {
        val state = _state.value
        val currentPlayer = (state.phase as? GamePhase.Playing)?.currentPlayer
        if (currentPlayer != PlayerId.HUMAN) return
        val pattern = CardEvaluator.determinePattern(cards)
        if (pattern.pattern == CardPattern.INVALID) return
        val previous = (state.phase as? GamePhase.Playing)?.lastAction
        if (previous is TurnAction.Play) {
            val prevResult = CardEvaluator.determinePattern(previous.cards)
            if (!CardEvaluator.canBeat(pattern, prevResult)) return
        }
        handlePlay(PlayerId.HUMAN, TurnAction.Play(cards, pattern.pattern))
    }

    fun humanPass() {
        handlePlay(PlayerId.HUMAN, TurnAction.Pass)
    }

    fun requestHint() {
        val state = _state.value
        val player = state.players[PlayerId.HUMAN] ?: return
        val previous = (state.phase as? GamePhase.Playing)?.lastAction
        val hint = bots.getValue(botDifficulty).chooseHint(player, previous)
        updateState { it.copy(hintSelection = hint) }
    }

    private fun launchAIAfterDelay(block: suspend () -> Unit) {
        viewModelScope.launch {
            delay(500)
            block()
        }
    }

    private inline fun updateState(transform: (GameUiState) -> GameUiState) {
        _state.value = transform(_state.value)
    }
}
