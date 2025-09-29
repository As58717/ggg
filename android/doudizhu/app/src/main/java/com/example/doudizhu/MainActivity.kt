package com.example.doudizhu

import android.media.MediaPlayer
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.example.doudizhu.model.AudioCue
import com.example.doudizhu.ui.GameScreen
import com.example.doudizhu.ui.theme.DoudizhuComposeTheme
import com.example.doudizhu.viewmodel.GameViewModel

class MainActivity : ComponentActivity() {
    private val viewModel: GameViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        viewModel.startGame()
        setContent {
            DoudizhuComposeTheme {
                val state by viewModel.state.collectAsStateWithLifecycle()
                Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    GameScreen(state = state, viewModel = viewModel)
                }

                val context = this
                LaunchedEffect(state.audioCue) {
                    when (state.audioCue) {
                        AudioCue.SHUFFLE -> playSound(context, R.raw.sfx_shuffle)
                        AudioCue.DEAL -> playSound(context, R.raw.sfx_deal)
                        AudioCue.PLAY -> playSound(context, R.raw.sfx_play)
                        AudioCue.PASS -> playSound(context, R.raw.sfx_pass)
                        AudioCue.WIN -> playSound(context, R.raw.sfx_win)
                        AudioCue.LOSE -> playSound(context, R.raw.sfx_lose)
                        null -> Unit
                    }
                }
            }
        }
    }

    private fun playSound(activity: ComponentActivity, resId: Int) {
        MediaPlayer.create(activity, resId)?.apply {
            setOnCompletionListener { it.release() }
            start()
        }
    }
}
