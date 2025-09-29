package com.example.doudizhu.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Lightbulb
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.SkipNext
import androidx.compose.material3.AssistChip
import androidx.compose.material3.AssistChipDefaults
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.doudizhu.R
import com.example.doudizhu.ai.Difficulty
import com.example.doudizhu.model.Card
import com.example.doudizhu.model.GameMode
import com.example.doudizhu.model.GamePhase
import com.example.doudizhu.model.GameUiState
import com.example.doudizhu.model.PlayerId
import com.example.doudizhu.model.TurnAction
import com.example.doudizhu.viewmodel.GameViewModel

@Composable
fun GameScreen(state: GameUiState, viewModel: GameViewModel) {
    val (selectedCards, setSelectedCards) = remember { mutableStateOf(emptyList<Card>()) }
    val (modeMenuExpanded, setModeMenuExpanded) = remember { mutableStateOf(false) }
    val (difficultyMenuExpanded, setDifficultyMenuExpanded) = remember { mutableStateOf(false) }

    LaunchedEffect(state.hintSelection) {
        if (state.hintSelection.isNotEmpty()) {
            setSelectedCards(state.hintSelection)
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.surface)
            .padding(16.dp)
    ) {
        TopAppBar(
            title = { Text("Compose 斗地主") },
            navigationIcon = {
                IconButton(onClick = { viewModel.startGame(state.mode) }) {
                    Icon(Icons.Default.Refresh, contentDescription = "重新开始")
                }
            },
            actions = {
                ModeSelector(state, setModeMenuExpanded, viewModel)
                DifficultySelector(setDifficultyMenuExpanded, viewModel)
            }
        )

        Spacer(modifier = Modifier.height(8.dp))

        HeaderInfo(state)

        Spacer(modifier = Modifier.height(12.dp))

        BoardArea(state)

        Spacer(modifier = Modifier.height(12.dp))

        HumanHandArea(
            cards = state.players[PlayerId.HUMAN]?.hand.orEmpty(),
            selectedCards = selectedCards,
            hintSelection = state.hintSelection,
            onCardTapped = { card ->
                setSelectedCards(selectedCards.toggle(card))
            }
        )

        Spacer(modifier = Modifier.height(12.dp))

        ControlBar(
            selectedCards = selectedCards,
            onPlay = {
                viewModel.humanPlaySelected(selectedCards)
                setSelectedCards(emptyList())
            },
            onPass = {
                viewModel.humanPass()
                setSelectedCards(emptyList())
            },
            onHint = {
                viewModel.requestHint()
            }
        )
    }
}

@Composable
private fun ModeSelector(state: GameUiState, expanded: MutableState<Boolean>, viewModel: GameViewModel) {
    Box {
        AssistChip(
            onClick = { expanded.value = true },
            label = { Text(text = if (state.mode == GameMode.THREE_PLAYER) "三人" else "四人") },
            leadingIcon = {
                Icon(
                    painter = painterResource(id = R.drawable.ic_mode_switch),
                    contentDescription = null
                )
            },
            colors = AssistChipDefaults.assistChipColors(containerColor = MaterialTheme.colorScheme.primaryContainer)
        )
        DropdownMenu(expanded = expanded.value, onDismissRequest = { expanded.value = false }) {
            GameMode.entries.forEach { mode ->
                DropdownMenuItem(
                    text = { Text(if (mode == GameMode.THREE_PLAYER) "三人模式" else "四人模式") },
                    onClick = {
                        expanded.value = false
                        viewModel.startGame(mode)
                    }
                )
            }
        }
    }
}

@Composable
private fun DifficultySelector(expanded: MutableState<Boolean>, viewModel: GameViewModel) {
    Box {
        AssistChip(
            onClick = { expanded.value = true },
            label = { Text("难度") },
            leadingIcon = { Icon(Icons.Default.Lightbulb, contentDescription = null) }
        )
        DropdownMenu(expanded = expanded.value, onDismissRequest = { expanded.value = false }) {
            Difficulty.entries.forEach { difficulty ->
                DropdownMenuItem(
                    text = { Text(if (difficulty == Difficulty.CASUAL) "入门" else "思考") },
                    onClick = {
                        expanded.value = false
                        viewModel.updateDifficulty(difficulty)
                    }
                )
            }
        }
    }
}

@Composable
private fun HeaderInfo(state: GameUiState) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(text = "倍数: ${state.multiplier}", fontWeight = FontWeight.Bold)
            Text(text = "底牌: ${state.bottomCards.size}")
            val landlordName = state.landlord?.let { id -> state.players[id]?.name } ?: "待定"
            Text(text = "地主: $landlordName")
        }
    }
}

@Composable
private fun BoardArea(state: GameUiState) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(220.dp),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        PlayerColumn(state, PlayerId.LEFT_AI)
        CenterBoard(state)
        if (state.mode == GameMode.FOUR_PLAYER) {
            PlayerColumn(state, PlayerId.TOP_AI)
        }
        PlayerColumn(state, PlayerId.RIGHT_AI)
    }
}

@Composable
private fun PlayerColumn(state: GameUiState, playerId: PlayerId) {
    val player = state.players[playerId]
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.SpaceBetween,
        modifier = Modifier.padding(8.dp)
    ) {
        Text(player?.name ?: "AI", fontWeight = FontWeight.SemiBold)
        CardPlaceholder(cardCount = state.remainingCards[playerId] ?: player?.hand?.size ?: 0)
        LastPlayView(state.lastPlayed[playerId])
    }
}

@Composable
private fun CenterBoard(state: GameUiState) {
    Column(
        modifier = Modifier
            .padding(horizontal = 16.dp)
            .weight(1f),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            text = when (val phase = state.phase) {
                is GamePhase.Bidding -> "叫分阶段：当前 ${phase.currentBid} 分"
                is GamePhase.Robbing -> "抢地主阶段：${phase.currentBid} 分"
                is GamePhase.Playing -> "轮到 ${state.players[phase.currentPlayer]?.name ?: "AI"}"
                is GamePhase.Settled -> "${state.players[phase.winner]?.name ?: "AI"} 获胜"
                GamePhase.Shuffling -> "洗牌中"
                GamePhase.Dealing -> "发牌中"
                else -> "准备中"
            },
            fontWeight = FontWeight.Bold,
            fontSize = 18.sp,
            modifier = Modifier.fillMaxWidth(),
            textAlign = TextAlign.Center
        )

        Spacer(modifier = Modifier.height(12.dp))

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(140.dp)
                .background(MaterialTheme.colorScheme.surfaceVariant, RoundedCornerShape(16.dp))
                .border(2.dp, MaterialTheme.colorScheme.outline, RoundedCornerShape(16.dp)),
            contentAlignment = Alignment.Center
        ) {
            val action = state.lastPlayed.entries.lastOrNull { it.value is TurnAction.Play }?.value as? TurnAction.Play
            if (action != null) {
                PlayedCards(cards = action.cards)
            } else {
                Text("等待出牌", color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}

@Composable
private fun PlayedCards(cards: List<Card>) {
    LazyRow(
        modifier = Modifier.padding(12.dp),
        horizontalArrangement = Arrangement.Center
    ) {
        items(cards) { card ->
            CardFace(card)
            Spacer(modifier = Modifier.size(4.dp))
        }
    }
}

@Composable
private fun LastPlayView(action: TurnAction?) {
    when (action) {
        is TurnAction.Play -> Row(verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Default.PlayArrow, contentDescription = null)
            Text(text = "${action.cards.size} 张", modifier = Modifier.padding(start = 4.dp))
        }
        TurnAction.Pass -> Text("不要", color = Color.Red)
        null -> Text("待出")
    }
}

@Composable
private fun HumanHandArea(
    cards: List<Card>,
    selectedCards: List<Card>,
    hintSelection: List<Card>,
    onCardTapped: (Card) -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(MaterialTheme.colorScheme.surfaceVariant, RoundedCornerShape(16.dp))
            .padding(12.dp)
    ) {
        Text("我的手牌", fontWeight = FontWeight.Bold)
        Spacer(modifier = Modifier.height(8.dp))
        LazyRow(horizontalArrangement = Arrangement.spacedBy(-40.dp)) {
            items(cards) { card ->
                val isSelected = selectedCards.contains(card) || hintSelection.contains(card)
                CardFace(
                    card = card,
                    modifier = Modifier
                        .padding(end = 40.dp)
                        .clickable { onCardTapped(card) }
                        .padding(top = if (isSelected) 0.dp else 16.dp)
                )
            }
        }
    }
}

@Composable
private fun ControlBar(
    selectedCards: List<Card>,
    onPlay: () -> Unit,
    onPass: () -> Unit,
    onHint: () -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceEvenly,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Button(onClick = onPlay, enabled = selectedCards.isNotEmpty()) {
            Icon(Icons.Default.PlayArrow, contentDescription = null)
            Text("出牌", modifier = Modifier.padding(start = 4.dp))
        }
        TextButton(onClick = onPass) {
            Icon(Icons.Default.SkipNext, contentDescription = null)
            Text("过")
        }
        Button(onClick = onHint) {
            Icon(Icons.Default.Lightbulb, contentDescription = null)
            Text("提示")
        }
    }
}

@Composable
private fun CardPlaceholder(cardCount: Int) {
    Box(contentAlignment = Alignment.Center) {
        Canvas(modifier = Modifier.size(60.dp)) {
            drawRoundRect(
                color = Color(0xFF3F51B5),
                style = Stroke(width = 4f),
                cornerRadius = androidx.compose.ui.geometry.CornerRadius(16f, 16f)
            )
        }
        Text(text = "$cardCount", color = Color.Black, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun CardFace(card: Card, modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .size(width = 80.dp, height = 120.dp)
            .background(Color.White, RoundedCornerShape(12.dp))
            .border(2.dp, Color.Black, RoundedCornerShape(12.dp))
            .padding(8.dp),
        contentAlignment = Alignment.TopStart
    ) {
        Column(
            verticalArrangement = Arrangement.SpaceBetween,
            modifier = Modifier.fillMaxSize()
        ) {
            Text(card.displayName, color = Color.Black, fontWeight = FontWeight.Bold)
            Image(
                painter = painterResource(id = R.drawable.ic_card_placeholder),
                contentDescription = null,
                modifier = Modifier.size(32.dp)
            )
        }
    }
}

private fun List<Card>.toggle(card: Card): List<Card> {
    val mutable = toMutableList()
    if (mutable.contains(card)) {
        mutable.remove(card)
    } else {
        mutable.add(card)
    }
    return mutable
}
