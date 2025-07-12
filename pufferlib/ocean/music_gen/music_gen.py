'''A high-performance audio synthesis RL environment'''

import gymnasium
import numpy as np

import pufferlib
from pufferlib.ocean.music_gen import binding

class MusicGen(pufferlib.PufferEnv):
    def __init__(self, num_envs=1, render_mode=None, log_interval=128, buf=None, seed=0):
        # Update observation space for 128-dimensional float observations
        self.single_observation_space = gymnasium.spaces.Box(
            low=0.0, high=1.0,
            shape=(128,), dtype=np.float32
        )
        
        # Update action space for 336-dimensional continuous actions
        self.single_action_space = gymnasium.spaces.Box(
            low=-1.0, high=1.0,
            shape=(336,), dtype=np.float32
        )
        
        self.render_mode = render_mode
        self.num_agents = num_envs

        super().__init__(buf)
        
        # Initialize C environments with proper data types
        self.c_envs = binding.vec_init(
            self.observations.astype(np.float32),  # 128-dim float observations
            self.actions.astype(np.float32),       # 336-dim float actions
            self.rewards, 
            self.terminals, 
            self.truncations, 
            num_envs, 
            seed
        )
 
    def reset(self, seed=0):
        binding.vec_reset(self.c_envs, seed)
        return self.observations, []

    def step(self, actions):
        # Ensure actions are the right type and shape
        self.actions[:] = actions.astype(np.float32)
        binding.vec_step(self.c_envs)
        info = [binding.vec_log(self.c_envs)]
        return (self.observations, self.rewards,
            self.terminals, self.truncations, info)

    def render(self):
        binding.vec_render(self.c_envs, 0)

    def close(self):
        binding.vec_close(self.c_envs)
    
    # TODO: export audio for evals
    # def export_audio(self, env_idx=0, filename="output.wav"):
    #     """Export audio from a specific environment to WAV file"""
    #     return binding.vec_export_audio(self.c_envs, env_idx, filename.encode('utf-8'))

if __name__ == '__main__':
    print("Testing PufferLib Audio RL Environment...")
    
    N = 1024  # Number of parallel environments
    env = MusicGen(num_envs=N)
    obs, _ = env.reset()
    
    print(f"Environment initialized with {N} parallel instances")
    print(f"Observation shape: {obs.shape}")
    print(f"Action space: {env.single_action_space}")
    print(f"Observation space: {env.single_observation_space}")
    
    steps = 0
    CACHE = 512
    
    # Generate random actions in the correct range [-1, 1] for continuous control
    actions = np.random.uniform(-1.0, 1.0, (CACHE, N, 336)).astype(np.float32)
    
    print(f"Action shape: {actions.shape}")
    print("Starting performance test...")

    import time
    start = time.time()
    total_reward = 0.0
    
    while time.time() - start < 10:
        obs, rewards, terminals, truncations, info = env.step(actions[steps % CACHE])
        total_reward += np.mean(rewards)
        steps += 1
        
        # Print progress every 100 steps
        if steps % 100 == 0:
            elapsed = time.time() - start
            sps = env.num_agents * steps / elapsed
            avg_reward = total_reward / steps
            print(f"Step {steps}: SPS={int(sps)}, Avg Reward={avg_reward:.4f}")

    elapsed = time.time() - start
    sps = env.num_agents * steps / elapsed
    avg_reward = total_reward / steps
    
    print(f"\nPerformance Test Results:")
    print(f"Total Steps: {steps}")
    print(f"Total Time: {elapsed:.2f}s") 
    print(f"Steps Per Second: {int(sps)}")
    print(f"Average Reward: {avg_reward:.4f}")
    
    # Test audio export
    # print("\nTesting audio export...")
    # try:
    #     success = env.export_audio(0, "test_output.wav")
    #     if success:
    #         print("✓ Audio export successful: test_output.wav")
    #     else:
    #         print("✗ Audio export failed")
    # except Exception as e:
    #     print(f"✗ Audio export error: {e}")
    
    env.close()
    print("Environment closed successfully")