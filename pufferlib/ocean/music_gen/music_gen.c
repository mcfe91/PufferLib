#include "music_gen.h"
#include <stdio.h>
#include <time.h>

int main() {
    // Initialize random seed
    srand(time(NULL));
    
    MusicGen env = {0};
    
    // Allocate memory for observations (128 floats), actions (336 floats), rewards, terminals
    env.observations = (float*)calloc(OBS_DIM, sizeof(float));
    env.actions = (float*)calloc(ACTION_DIM, sizeof(float));
    env.rewards = (float*)calloc(1, sizeof(float));
    env.terminals = (unsigned char*)calloc(1, sizeof(unsigned char));
    
    if (!env.observations || !env.actions || !env.rewards || !env.terminals) {
        printf("Failed to allocate memory!\n");
        return -1;
    }

    printf("Starting PufferLib Music Generator...\n");
    printf("Controls:\n");
    printf("  SHIFT + Keys: Manual control\n");
    printf("  No SHIFT: Random actions\n");
    printf("  ESC: Exit\n");
    printf("  SPACE: Reset environment\n");
    printf("  R: Generate random action burst\n");
    
    c_reset(&env);
    c_render(&env);
    
    int step_count = 0;
    float total_reward = 0.0f;
    
    while (!WindowShouldClose()) {
        // Handle input for manual control
        if (IsKeyDown(KEY_LEFT_SHIFT)) {
            // Manual control mode - zero out actions first
            for (int i = 0; i < ACTION_DIM; i++) {
                env.actions[i] = 0.0f;
            }
            
            // Track volume controls (Q/W, A/S, Z/X, 1/2)
            if (IsKeyDown(KEY_Q)) env.actions[0] = 0.5f;  // Track 0 volume up
            if (IsKeyDown(KEY_W)) env.actions[0] = -0.5f; // Track 0 volume down
            if (IsKeyDown(KEY_A)) env.actions[1] = 0.5f;  // Track 1 volume up  
            if (IsKeyDown(KEY_S)) env.actions[1] = -0.5f; // Track 1 volume down
            if (IsKeyDown(KEY_Z)) env.actions[2] = 0.5f;  // Track 2 volume up
            if (IsKeyDown(KEY_X)) env.actions[2] = -0.5f; // Track 2 volume down
            if (IsKeyDown(KEY_ONE)) env.actions[3] = 0.5f;  // Track 3 volume up
            if (IsKeyDown(KEY_TWO)) env.actions[3] = -0.5f; // Track 3 volume down
            
            // Filter controls (E/R, D/F, C/V, 3/4)
            if (IsKeyDown(KEY_E)) env.actions[4] = 0.3f;  // Track 0 filter up
            if (IsKeyDown(KEY_R)) env.actions[4] = -0.3f; // Track 0 filter down
            if (IsKeyDown(KEY_D)) env.actions[5] = 0.3f;  // Track 1 filter up
            if (IsKeyDown(KEY_F)) env.actions[5] = -0.3f; // Track 1 filter down
            if (IsKeyDown(KEY_C)) env.actions[6] = 0.3f;  // Track 2 filter up
            if (IsKeyDown(KEY_V)) env.actions[6] = -0.3f; // Track 2 filter down
            if (IsKeyDown(KEY_THREE)) env.actions[7] = 0.3f;  // Track 3 filter up
            if (IsKeyDown(KEY_FOUR)) env.actions[7] = -0.3f; // Track 3 filter down
            
            // Oscillator mix controls (T/Y, G/H, B/N, 5/6)
            if (IsKeyDown(KEY_T)) env.actions[8] = 0.4f;   // Track 0 osc mix
            if (IsKeyDown(KEY_Y)) env.actions[8] = -0.4f;
            if (IsKeyDown(KEY_G)) env.actions[9] = 0.4f;   // Track 1 osc mix
            if (IsKeyDown(KEY_H)) env.actions[9] = -0.4f;
            if (IsKeyDown(KEY_B)) env.actions[10] = 0.4f;  // Track 2 osc mix
            if (IsKeyDown(KEY_N)) env.actions[10] = -0.4f;
            if (IsKeyDown(KEY_FIVE)) env.actions[11] = 0.4f;  // Track 3 osc mix
            if (IsKeyDown(KEY_SIX)) env.actions[11] = -0.4f;
            
            // Sequencer controls - activate/deactivate notes
            // Use number row for track 0 steps 0-9
            if (IsKeyPressed(KEY_ZERO)) {
                env.actions[16] = 1.0f; // Activate step 0, track 0
            }
            // Arrow keys for pitch adjustments
            if (IsKeyDown(KEY_UP)) {
                for (int i = 17; i < 80; i += 4) { // Pitch actions
                    env.actions[i] = 0.2f; // Pitch up
                }
            }
            if (IsKeyDown(KEY_DOWN)) {
                for (int i = 17; i < 80; i += 4) { // Pitch actions
                    env.actions[i] = -0.2f; // Pitch down
                }
            }
            
        } else if (IsKeyPressed(KEY_R)) {
            // Generate random action burst
            for (int i = 0; i < ACTION_DIM; i++) {
                env.actions[i] = ((float)rand() / RAND_MAX) * 2.0f - 1.0f; // Random [-1, 1]
            }
        } else {
            // Random mode - generate small random actions
            for (int i = 0; i < ACTION_DIM; i++) {
                env.actions[i] = ((float)rand() / RAND_MAX) * 2.0f - 1.0f; // Random [-1, 1]
            }
        }
        
        // Reset environment if SPACE is pressed
        if (IsKeyPressed(KEY_SPACE)) {
            printf("Resetting environment...\n");
            c_reset(&env);
            step_count = 0;
            total_reward = 0.0f;
        }
        
        // Step the environment
        c_step(&env);
        
        // Update statistics
        step_count++;
        total_reward += env.rewards[0];
        
        // Print stats every 60 frames (roughly 2 seconds at 30 FPS)
        if (step_count % 60 == 0) {
            printf("Step %d: Reward %.4f, Avg Reward: %.4f\n", 
                   step_count, env.rewards[0], total_reward / step_count);
        }
        
        // Render the environment
        c_render(&env);
    }
    
    // Print final statistics
    printf("\nFinal Statistics:\n");
    printf("Total Steps: %d\n", step_count);
    printf("Total Reward: %.4f\n", total_reward);
    printf("Average Reward: %.4f\n", step_count > 0 ? total_reward / step_count : 0.0f);
    
    // Clean up
    free(env.observations);
    free(env.actions);
    free(env.rewards);
    free(env.terminals);
    c_close(&env);
    
    printf("Goodbye!\n");
    return 0;
}