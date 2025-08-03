#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include "raylib.h"

// Only use floats!
typedef struct {
    float score;
    float n; // Required as the last field 
} Log;

// Client struct for graphics
typedef struct Client {
    int width;
    int height;
} Client;

// Helper functions for C compatibility
static inline float min_f(float a, float b) { return (a < b) ? a : b; }
static inline float max_f(float a, float b) { return (a > b) ? a : b; }
static inline int min_i(int a, int b) { return (a < b) ? a : b; }
static inline int max_i(int a, int b) { return (a > b) ? a : b; }

const Color PUFF_RED = (Color){187, 0, 0, 255};
const Color PUFF_CYAN = (Color){0, 187, 187, 255};
const Color PUFF_WHITE = (Color){241, 241, 241, 241};
const Color PUFF_BACKGROUND = (Color){6, 24, 24, 255};
const Color PUFF_GREEN = (Color){0, 187, 0, 255};
const Color PUFF_YELLOW = (Color){187, 187, 0, 255};

// Audio engine constants
#define NUM_TRACKS 4
#define OBS_DIM 290  // 32 + 256 + 2
#define ACTION_DIM 290 // 32 + 256 + 2
#define SAMPLE_RATE 44100
#define BUFFER_SIZE SAMPLE_RATE * 2 // 1 bar at 120 BPM (2 seconds)

// Audio environment structure (embedded directly)
typedef struct {
    // Audio buffers
    float track_buffers[NUM_TRACKS][BUFFER_SIZE];
    float master_buffer[BUFFER_SIZE];
    
    // Synth state (per track)
    struct {
        float oscillator_mix;      // 0-1: Sine to saw mix
        float filter_cutoff;       // 0-1: 20Hz to 20kHz
        float filter_resonance;    // 0-1: No resonance to high resonance
        float envelope_attack;     // 0-1: 0ms to 1000ms
        float envelope_release;    // 0-1: 0ms to 2000ms
        float pitch;               // MIDI note (36-84)
        float volume;              // 0-1: Silent to unity gain
        float pan;                 // -1 to 1: Left to right
        
        // Filter state variables
        float filter_state[2];
        
        struct Note {
            bool active;              // Is the note playing
            float pitch;              // Absolute MIDI note value
            int duration_samples;     // Duration in samples
            float velocity;           // Volume/intensity (0.0-1.0)
        } notes[16];                  // 16-step pattern
    } tracks[NUM_TRACKS];
    
    // Global state
    int current_step;              // Current sequencer step
    float tempo;                   // BPM
    float master_volume;           // 0-1: Master volume
    
    // Global effects
    struct {
        float reverb_size;         // 0-1: Room size
        float reverb_damping;      // 0-1: High frequency damping
        float delay_time;          // 0-1: 0ms to 1000ms
        float delay_feedback;      // 0-1: No feedback to high feedback
        
        // Effect state variables
        float reverb_buffer[8192]; // Reverb memory
        float delay_buffer[SAMPLE_RATE]; // 1 second delay
        int delay_index;           // Current position in delay buffer
    } effects;
} AudioEnvironment;

typedef struct {
    Log log;                     // Required field
    Client* client;              // Graphics client
    float* observations;         // 290-dimensional float observations
    float* actions;              // 290-dimensional float actions
    float* rewards;              // Required field
    unsigned char* terminals;    // Required field
    
    // Audio engine state
    AudioEnvironment audio_env;  // Embedded audio environment
} MusicGen;

// Initialize audio environment
void init_audio_environment(AudioEnvironment* env) {
    // Clear audio buffers
    memset(env->track_buffers, 0, sizeof(env->track_buffers));
    memset(env->master_buffer, 0, sizeof(env->master_buffer));
    
    // Initialize tracks with default values
    for (int t = 0; t < NUM_TRACKS; t++) {
        env->tracks[t].oscillator_mix = 0.0f;      // Pure sine
        env->tracks[t].filter_cutoff = 0.8f;       // Fairly open filter
        env->tracks[t].filter_resonance = 0.2f;    // Slight resonance
        env->tracks[t].envelope_attack = 0.1f;     // Quick attack
        env->tracks[t].envelope_release = 0.5f;    // Medium release
        env->tracks[t].pitch = 48.0f + t * 12.0f;  // C3, C4, C5, C6
        env->tracks[t].volume = 0.7f;              // 70% volume
        env->tracks[t].pan = (t % 2 == 0) ? -0.3f : 0.3f; // Alternate panning
        
        // Clear filter state
        memset(env->tracks[t].filter_state, 0, sizeof(env->tracks[t].filter_state));
        
        // Initialize notes
        for (int s = 0; s < 16; s++) {
            env->tracks[t].notes[s].active = false;
            env->tracks[t].notes[s].pitch = 48.0f + t * 12.0f;  // C3, C4, C5, C6
            env->tracks[t].notes[s].duration_samples = BUFFER_SIZE / 16; // One step
            env->tracks[t].notes[s].velocity = 0.7f;
        }
    }
    
    // Initialize global state
    env->current_step = 0;
    env->tempo = 120.0f;
    env->master_volume = 0.8f;
    
    // Initialize effects
    env->effects.reverb_size = 0.3f;
    env->effects.reverb_damping = 0.5f;
    env->effects.delay_time = 0.25f;
    env->effects.delay_feedback = 0.3f;
    
    // Clear effect buffers
    memset(env->effects.reverb_buffer, 0, sizeof(env->effects.reverb_buffer));
    memset(env->effects.delay_buffer, 0, sizeof(env->effects.delay_buffer));
    env->effects.delay_index = 0;
}

// Generate audio for a full bar
void generate_audio(AudioEnvironment* env) {
    // Clear master buffer
    memset(env->master_buffer, 0, sizeof(env->master_buffer));
    
    // Calculate samples per step
    int samples_per_step = BUFFER_SIZE / 16;
    
    // Process each track
    for (int t = 0; t < NUM_TRACKS; t++) {
        // Clear track buffer
        memset(env->track_buffers[t], 0, sizeof(env->track_buffers[t]));
        
        // Process each step in the pattern
        for (int s = 0; s < 16; s++) {
            if (env->tracks[t].notes[s].active) {
                int step_start = s * samples_per_step;
                int note_duration = env->tracks[t].notes[s].duration_samples;
                int step_end = step_start + note_duration;
                step_end = min_i(step_end, BUFFER_SIZE);
                
                // Calculate frequency from MIDI note
                float frequency = 440.0f * powf(2.0f, (env->tracks[t].notes[s].pitch - 69.0f) / 12.0f);
                float mix = env->tracks[t].oscillator_mix;
                
                // Generate waveform
                for (int i = step_start; i < step_end; i++) {
                    float sample_offset = (float)(i - step_start);
                    float phase = sample_offset / SAMPLE_RATE * frequency;
                    phase -= floorf(phase); // Normalize to 0-1
                    
                    // Generate sine and saw components
                    float sine_val = sinf(phase * 2.0f * M_PI);
                    float saw_val = 2.0f * phase - 1.0f;
                    
                    // Mix between sine and saw
                    env->track_buffers[t][i] += (sine_val * (1.0f - mix) + saw_val * mix) * env->tracks[t].notes[s].velocity;
                }
                
                // Apply envelope and filter (simplified for brevity)
                float attack = env->tracks[t].envelope_attack * SAMPLE_RATE * 0.1f;
                
                for (int i = step_start; i < step_end; i++) {
                    float envelope = 1.0f;
                    float position = (float)(i - step_start);
                    
                    if (position < attack) {
                        envelope = position / attack;
                    }
                    
                    env->track_buffers[t][i] *= envelope;
                }
            }
        }
        
        // Apply track volume and add to master
        float volume = env->tracks[t].volume;
        for (int i = 0; i < BUFFER_SIZE; i++) {
            env->master_buffer[i] += env->track_buffers[t][i] * volume;
        }
    }
    
    // Apply master volume and limiting
    for (int i = 0; i < BUFFER_SIZE; i++) {
        env->master_buffer[i] *= env->master_volume;
        if (env->master_buffer[i] > 1.0f) env->master_buffer[i] = 1.0f;
        if (env->master_buffer[i] < -1.0f) env->master_buffer[i] = -1.0f;
    }
}

// Apply action to environment
void apply_action(AudioEnvironment* env, const float* action) {
    int action_idx = 0;
    
    // Track parameters: 8 parameters per track × 4 tracks = 32 actions
    for (int t = 0; t < NUM_TRACKS && action_idx < ACTION_DIM; t++) {
        // Oscillator mix
        if (action_idx < ACTION_DIM) {
            env->tracks[t].oscillator_mix += action[action_idx] * 0.1f;
            env->tracks[t].oscillator_mix = min_f(1.0f, max_f(0.0f, env->tracks[t].oscillator_mix));
            action_idx++;
        }
        
        // Filter cutoff
        if (action_idx < ACTION_DIM) {
            env->tracks[t].filter_cutoff += action[action_idx] * 0.1f;
            env->tracks[t].filter_cutoff = min_f(1.0f, max_f(0.0f, env->tracks[t].filter_cutoff));
            action_idx++;
        }
        
        // Filter resonance
        if (action_idx < ACTION_DIM) {
            env->tracks[t].filter_resonance += action[action_idx] * 0.1f;
            env->tracks[t].filter_resonance = min_f(1.0f, max_f(0.0f, env->tracks[t].filter_resonance));
            action_idx++;
        }
        
        // Envelope attack
        if (action_idx < ACTION_DIM) {
            env->tracks[t].envelope_attack += action[action_idx] * 0.1f;
            env->tracks[t].envelope_attack = min_f(1.0f, max_f(0.0f, env->tracks[t].envelope_attack));
            action_idx++;
        }
        
        // Envelope release
        if (action_idx < ACTION_DIM) {
            env->tracks[t].envelope_release += action[action_idx] * 0.1f;
            env->tracks[t].envelope_release = min_f(1.0f, max_f(0.0f, env->tracks[t].envelope_release));
            action_idx++;
        }
        
        // Track-level pitch
        if (action_idx < ACTION_DIM) {
            env->tracks[t].pitch += action[action_idx] * 12.0f; // ±12 semitones
            env->tracks[t].pitch = min_f(84.0f, max_f(36.0f, env->tracks[t].pitch)); // C3 to C7
            action_idx++;
        }
        
        // Volume
        if (action_idx < ACTION_DIM) {
            env->tracks[t].volume += action[action_idx] * 0.1f;
            env->tracks[t].volume = min_f(1.0f, max_f(0.0f, env->tracks[t].volume));
            action_idx++;
        }
        
        // Pan
        if (action_idx < ACTION_DIM) {
            env->tracks[t].pan += action[action_idx] * 0.1f;
            env->tracks[t].pan = min_f(1.0f, max_f(-1.0f, env->tracks[t].pan));
            action_idx++;
        }
    }
    
    // Sequencer patterns: 4 tracks × 16 steps × 4 params = 256 actions
    int samples_per_step = BUFFER_SIZE / 16;
    
    for (int t = 0; t < NUM_TRACKS && action_idx < ACTION_DIM; t++) {
        for (int s = 0; s < 16 && action_idx < ACTION_DIM; s++) {
            // On/off state
            if (action_idx < ACTION_DIM) {
                if (action[action_idx] > 0.1f) {
                    env->tracks[t].notes[s].active = true;
                } else if (action[action_idx] < -0.1f) {
                    env->tracks[t].notes[s].active = false;
                }
                action_idx++;
            }
            
            // Note-level pitch (relative to track pitch)
            if (action_idx < ACTION_DIM) {
                float pitch_offset = action[action_idx] * 12.0f; // ±12 semitones from track pitch
                env->tracks[t].notes[s].pitch = env->tracks[t].pitch + pitch_offset;
                env->tracks[t].notes[s].pitch = min_f(84.0f, max_f(36.0f, env->tracks[t].notes[s].pitch));
                action_idx++;
            }
            
            // Duration
            if (action_idx < ACTION_DIM) {
                float steps_duration = (action[action_idx] + 1.0f) * 2.0f; // 0-4 steps
                env->tracks[t].notes[s].duration_samples = (int)(steps_duration * samples_per_step);
                env->tracks[t].notes[s].duration_samples = min_i(BUFFER_SIZE, max_i(0, env->tracks[t].notes[s].duration_samples));
                action_idx++;
            }
            
            // Velocity
            if (action_idx < ACTION_DIM) {
                env->tracks[t].notes[s].velocity = (action[action_idx] + 1.0f) * 0.5f; // 0-1 range
                env->tracks[t].notes[s].velocity = min_f(1.0f, max_f(0.0f, env->tracks[t].notes[s].velocity));
                action_idx++;
            }
        }
    }
    
    // Global parameters: 2 actions
    // Tempo
    if (action_idx < ACTION_DIM) {
        env->tempo += action[action_idx] * 10.0f; // ±10 BPM
        env->tempo = min_f(200.0f, max_f(60.0f, env->tempo)); // 60-200 BPM
        action_idx++;
    }
    
    // Master volume
    if (action_idx < ACTION_DIM) {
        env->master_volume += action[action_idx] * 0.1f;
        env->master_volume = min_f(1.0f, max_f(0.0f, env->master_volume));
        action_idx++;
    }
}

// Calculate reward based on audio quality
float calculate_reward(AudioEnvironment* env) {
    // Extract basic audio features for reward calculation
    float rms = 0.0f;         // RMS amplitude
    float peak = 0.0f;        // Peak amplitude
    float spectral_centroid = 0.0f;  // Simplified approximation
    float dynamic_range = 0.0f;
    
    // Calculate RMS and peak
    for (int i = 0; i < BUFFER_SIZE; i++) {
        float sample = env->master_buffer[i];
        rms += sample * sample;
        if (fabs(sample) > peak) peak = fabs(sample);
    }
    rms = sqrtf(rms / BUFFER_SIZE);
    
    // Approximate spectral centroid using zero-crossing rate (very simplified)
    int zero_crossings = 0;
    for (int i = 1; i < BUFFER_SIZE; i++) {
        if ((env->master_buffer[i] >= 0 && env->master_buffer[i-1] < 0) ||
            (env->master_buffer[i] < 0 && env->master_buffer[i-1] >= 0)) {
            zero_crossings++;
        }
    }
    spectral_centroid = (float)zero_crossings / BUFFER_SIZE;
    
    // Approximate dynamic range
    float sum_of_diffs = 0.0f;
    for (int i = 1; i < BUFFER_SIZE; i++) {
        sum_of_diffs += fabs(env->master_buffer[i] - env->master_buffer[i-1]);
    }
    dynamic_range = sum_of_diffs / BUFFER_SIZE;
    
    // Calculate reward components
    float reward = 0.0f;
    
    // Reward for appropriate loudness (not too quiet, not clipping)
    if (rms > 0.1f && peak < 0.95f) {
        reward += 2.0f * rms;
    } else if (peak > 1.0f) {
        reward -= 2.0f; // Penalty for clipping
    } else if (rms < 0.05f) {
        reward -= 1.0f; // Penalty for too quiet
    }
    
    // Reward for spectral balance
    if (spectral_centroid > 0.05f && spectral_centroid < 0.3f) {
        reward += 1.0f;
    }
    
    // Reward for dynamics
    reward += dynamic_range * 5.0f;
    
    // Bonus for track separation
    float track_separation = 0.0f;
    for (int t = 0; t < NUM_TRACKS; t++) {
        if (env->tracks[t].pan < -0.2f || env->tracks[t].pan > 0.2f) {
            track_separation += 0.25f;
        }
    }
    reward += track_separation;
    
    return reward;
}

// Write observations 
void write_observations(AudioEnvironment* env, float* obs_ptr) {
    int obs_idx = 0;
    int samples_per_step = BUFFER_SIZE / 16;
    
    // Write track parameters (4 tracks * 8 parameters = 32 values)
    for (int t = 0; t < NUM_TRACKS && obs_idx < OBS_DIM; t++) {
        if (obs_idx < OBS_DIM) obs_ptr[obs_idx++] = env->tracks[t].oscillator_mix;
        if (obs_idx < OBS_DIM) obs_ptr[obs_idx++] = env->tracks[t].filter_cutoff;
        if (obs_idx < OBS_DIM) obs_ptr[obs_idx++] = env->tracks[t].filter_resonance;
        if (obs_idx < OBS_DIM) obs_ptr[obs_idx++] = env->tracks[t].envelope_attack;
        if (obs_idx < OBS_DIM) obs_ptr[obs_idx++] = env->tracks[t].envelope_release;
        if (obs_idx < OBS_DIM) obs_ptr[obs_idx++] = (env->tracks[t].pitch - 36.0f) / 48.0f;
        if (obs_idx < OBS_DIM) obs_ptr[obs_idx++] = env->tracks[t].volume;
        if (obs_idx < OBS_DIM) obs_ptr[obs_idx++] = (env->tracks[t].pan + 1.0f) * 0.5f;
    }
    
    // Write sequencer state (4 tracks * 16 steps * 4 params = 256 values)
    for (int t = 0; t < NUM_TRACKS && obs_idx < OBS_DIM; t++) {
        for (int s = 0; s < 16 && obs_idx < OBS_DIM; s++) {
            // Active state (0.0 or 1.0)
            if (obs_idx < OBS_DIM) 
                obs_ptr[obs_idx++] = env->tracks[t].notes[s].active ? 1.0f : 0.0f;
            
            // Pitch (normalized to 0-1 range)
            if (obs_idx < OBS_DIM) 
                obs_ptr[obs_idx++] = (env->tracks[t].notes[s].pitch - 36.0f) / 48.0f;
            
            // Duration (normalized)
            if (obs_idx < OBS_DIM) 
                obs_ptr[obs_idx++] = (float)env->tracks[t].notes[s].duration_samples / (samples_per_step * 4.0f);
            
            // Velocity (already 0-1)
            if (obs_idx < OBS_DIM) 
                obs_ptr[obs_idx++] = env->tracks[t].notes[s].velocity;
        }
    }
    
    // Write global parameters
    if (obs_idx < OBS_DIM) obs_ptr[obs_idx++] = (env->tempo - 60.0f) / 120.0f;
    if (obs_idx < OBS_DIM) obs_ptr[obs_idx++] = env->master_volume;
}

// PufferLib interface functions
void c_reset(MusicGen* env) {
    init_audio_environment(&env->audio_env);
    
    write_observations(&env->audio_env, env->observations);
}

void c_step(MusicGen* env) {
    env->rewards[0] = 0;
    env->terminals[0] = 0;

    // Apply action and generate audio
    apply_action(&env->audio_env, env->actions);
    generate_audio(&env->audio_env);
    
    // Calculate reward
    env->rewards[0] = calculate_reward(&env->audio_env);
    
    // Write new observations
    write_observations(&env->audio_env, env->observations);
    
    // Audio environments typically don't terminate
    env->terminals[0] = 0;
    
    // Update log
    env->log.score += env->rewards[0];
    env->log.n += 1.0;
}

void c_render(MusicGen* env) {
    if (IsKeyDown(KEY_ESCAPE)) {
        exit(0);
    }
    
    if (env->client == NULL) {
        InitWindow(1080, 720, "PufferLib Music Generator");
        SetTargetFPS(60);
        env->client = (Client*)calloc(1, sizeof(Client));
        env->client->width = 1080;
        env->client->height = 720;
    }
    
    BeginDrawing();
    ClearBackground(PUFF_BACKGROUND);
    
    // Draw title
    DrawText("Audio RL Environment", 20, 20, 24, PUFF_WHITE);
    
    // Draw current reward
    char reward_text[64];
    sprintf(reward_text, "Reward: %.3f", env->rewards[0]);
    DrawText(reward_text, 20, 60, 20, PUFF_WHITE);
    
    // Draw track states
    for (int t = 0; t < NUM_TRACKS; t++) {
        int y_offset = 120 + t * 120;
        
        // Track label
        char track_text[32];
        sprintf(track_text, "Track %d", t + 1);
        DrawText(track_text, 20, y_offset, 18, PUFF_WHITE);
        
        // Volume bar
        int volume_width = (int)(env->audio_env.tracks[t].volume * 200);
        DrawRectangle(120, y_offset, volume_width, 20, PUFF_GREEN);
        DrawRectangleLines(120, y_offset, 200, 20, PUFF_WHITE);
        
        // Step pattern (16 steps)
        for (int s = 0; s < 16; s++) {
            int x = 120 + s * 30;
            int y = y_offset + 30;
            Color step_color = env->audio_env.tracks[t].notes[s].active ? PUFF_CYAN : PUFF_RED;
            DrawRectangle(x, y, 25, 25, step_color);
            DrawRectangleLines(x, y, 25, 25, PUFF_WHITE);
        }
        
        // Parameters
        char param_text[128];
        sprintf(param_text, "Vol:%.2f Cut:%.2f Mix:%.2f Pan:%.2f", 
                env->audio_env.tracks[t].volume,
                env->audio_env.tracks[t].filter_cutoff,
                env->audio_env.tracks[t].oscillator_mix,
                env->audio_env.tracks[t].pan);
        DrawText(param_text, 120, y_offset + 65, 14, PUFF_WHITE);
    }
    
    // Draw waveform (simplified)
    int waveform_y = 650;
    int waveform_samples = 1000; // Sample down for display
    int step = BUFFER_SIZE / waveform_samples;
    
    for (int i = 0; i < waveform_samples - 1; i++) {
        float sample1 = env->audio_env.master_buffer[i * step] * 50; // Scale for display
        float sample2 = env->audio_env.master_buffer[(i + 1) * step] * 50;
        
        DrawLine(20 + i, waveform_y + (int)sample1, 
                21 + i, waveform_y + (int)sample2, PUFF_YELLOW);
    }
    
    EndDrawing();
}

void c_close(MusicGen* env) {
    if (env->client != NULL) {
        CloseWindow();
        free(env->client);
    }
}