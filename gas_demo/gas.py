import math, random
import os

import gym
import numpy as np
import json
import argparse

import torch
import torch.nn as nn
import torch.optim as optim
import torch.nn.functional as F

from collections import deque

from acrobot import AcrobotEnv
from continuous_mountain_car import Continuous_MountainCarEnv

parser = argparse.ArgumentParser()
parser.add_argument("--batch_size", default=128, type=int)
parser.add_argument("--buffer_size", default=10000, type=int)
parser.add_argument("--target_update_interval", default=200, type=int)
parser.add_argument("--double_q", default=True, action="store_true")
parser.add_argument("--gamma", default=0.998, type=float)
parser.add_argument("--env_id", default="Acrobot", type=str)
parser.add_argument("--epsilon_start", default=1.0, type=float)
parser.add_argument("--epsilon_final", default=0.1, type=float)
parser.add_argument("--epsilon_lead_in", default=0, type=int)
parser.add_argument("--num_frames", default=200000, type=int)
parser.add_argument("--epsilon_decay", default=25000, type=int)
parser.add_argument("--lod_lead_in", default=25000, type=int)
parser.add_argument("--lod_plateau", default=25000, type=int)
parser.add_argument("--lod_growth", default=25000, type=int)
parser.add_argument("--lod_in_state", default=False, type=bool)
parser.add_argument("--prefix", default="results/experimental/test", type=str)
parser.add_argument("--do_evals", default=False, action="store_true")
parser.add_argument("--lr_decay", default=False, action="store_true")
parser.add_argument("--n_levels", default=1, type=int)
parser.add_argument("--frameskip", default=1, type=int)
parser.add_argument("--train_every", default=4, type=int)
parser.add_argument("--only_train_last", default=False, action="store_true")
parser.add_argument("--max_targets", default=False, action="store_true")
parser.add_argument("--delta_reg_coef", default=0.0, type=float)
parser.add_argument("--separate_models", default=False, action="store_true")
parser.add_argument("--separate_heads", default=False, action="store_true")
parser.add_argument("--n_repeats", default=5, type=int)

lod_in_state = False

Nonlin = nn.ReLU
fnonlin = F.relu

class ReplayBuffer(object):
    def __init__(self, capacity):
        self.buffer = deque(maxlen=capacity)

    def push(self, state, action, lod, reward, next_state, done):
        state      = np.expand_dims(state, 0)
        next_state = np.expand_dims(next_state, 0)

        self.buffer.append((state, action, lod, reward, next_state, done))

    def sample(self, batch_size):
        state, action, lod, reward, next_state, done = zip(*random.sample(self.buffer, batch_size))
        return np.concatenate(state), action, lod, reward, np.concatenate(next_state), done

    def __len__(self):
        return len(self.buffer)


class NaivePrioritizedBuffer(object):
    def __init__(self, capacity, prob_alpha=0.0):
        self.prob_alpha = prob_alpha
        self.capacity   = capacity
        self.buffer     = []
        self.pos        = 0
        self.priorities = np.zeros((capacity,), dtype=np.float32)

    def push(self, state, action, lod, reward, next_state, done):
        assert state.ndim == next_state.ndim
        state      = np.expand_dims(state, 0)
        next_state = np.expand_dims(next_state, 0)

        max_prio = self.priorities.max() if self.buffer else 1.0

        if len(self.buffer) < self.capacity:
            self.buffer.append((state, action, lod, reward, next_state, done))
        else:
            self.buffer[self.pos] = (state, action, lod, reward, next_state, done)

        self.priorities[self.pos] = max_prio
        self.pos = (self.pos + 1) % self.capacity

    def sample(self, batch_size, beta=0.4):
        if len(self.buffer) == self.capacity:
            prios = self.priorities
        else:
            prios = self.priorities[:self.pos]

        probs  = prios ** self.prob_alpha
        probs /= probs.sum()

        indices = np.random.choice(len(self.buffer), batch_size, p=probs)
        samples = [self.buffer[idx] for idx in indices]

        total    = len(self.buffer)
        batch       = list(zip(*samples))
        states      = np.concatenate(batch[0])
        actions     = batch[1]
        lods        = batch[2]
        rewards     = batch[3]
        next_states = np.concatenate(batch[4])
        dones       = batch[5]

        return states, actions, lods, rewards, next_states, dones, indices

    def update_priorities(self, batch_indices, batch_priorities):
        for idx, prio in zip(batch_indices, batch_priorities):
            self.priorities[idx] = prio

    def __len__(self):
        return len(self.buffer)


USE_CUDA = torch.cuda.is_available()


class DQN(nn.Module):
    def __init__(self, num_inputs, n_levels, separate_models=False, separate_heads=False):
        super(DQN, self).__init__()
        self.separate_models = separate_models
        self.separate_heads = separate_heads
        if separate_models:
            self.encoders = nn.ModuleList()
        else:
            self.encoder = nn.Sequential(
                nn.Linear(env.observation_space.shape[0] + lod_in_state, 128),
                Nonlin(),
                nn.Linear(128, 64),
                Nonlin(),
            )
        self.n_levels = n_levels
        self.n_actions = 2**n_levels

        self.decoders = nn.ModuleList()
        self.evaluators = nn.ModuleList()
        for i in range(n_levels):
            if self.separate_models:
                self.encoders.append( nn.Sequential(
                    nn.Linear(env.observation_space.shape[0], 128),
                    Nonlin(),
                    nn.Linear(128, 64),
                    Nonlin(),
                ))
            self.decoders.append(nn.Linear(64,64))
            self.evaluators.append(nn.Linear(64,2**(i+1)))
            if i > 0:
                self.evaluators[-1].bias.detach().zero_()
                self.evaluators[-1].weight.detach().mul_(0.01)

    def forward(self, x):
        bs = x.size(0)
        allQs = []
        allDeltas = []

        if self.separate_models:
            for i in range(self.n_levels):
                embed = self.encoders[i](x)
                embed = fnonlin(self.decoders[i](embed))
                levelQ = self.evaluators[i](embed)
                if i == 0 or self.separate_heads:
                    totalQ = levelQ
                else:
                    totalQ = totalQ.unsqueeze(-1).repeat(1,1,2).view(bs,-1) + levelQ
                    allDeltas.append(levelQ)
                allQs.append(totalQ)
        else:
            embed = self.encoder(x)
            for i in range(self.n_levels):
                embed = fnonlin(self.decoders[i](embed))
                levelQ = self.evaluators[i](embed)

                if i == 0 or self.separate_heads:
                    totalQ = levelQ
                else:
                    totalQ = totalQ.unsqueeze(-1).repeat(1,1,2).view(bs,-1) + levelQ
                    allDeltas.append(levelQ)
                allQs.append(totalQ)
        return allQs, allDeltas

    def act(self, state, epsilon, lod):
        if random.random() > epsilon:
            with torch.no_grad():
                state   = torch.tensor(state, dtype=torch.float).unsqueeze(0)
                q_values, _ = self.forward(state)
            q_value = q_values[lod]
            action  = q_value.max(1)[1].item()
        else:
            action = random.randrange(2**(lod+1))
        return action


def expand_q_values(allQ, n_levels):
    expandedQs = []
    for i, q in enumerate(allQ):
        expandedQs.append(q.unsqueeze(-1).expand(-1,-1,2**(n_levels-i-1)).reshape(q.size(0), -1))
    return expandedQs


def compute_td_loss(batch_size,
                    gamma,
                    n_levels,
                    num_frames, # for lr decay
                    temporal_consistency=False,
                    only_train_last=False,
                    max_targets=False,
                    delta_reg_coef=0.0,
                    double_q=True,
                    lr_decay=False
                    ):
    state, action, lod, reward, next_state, done, indices = replay_buffer.sample(batch_size)

    state      = torch.tensor(state, dtype=torch.float)
    next_state = torch.tensor(next_state, dtype=torch.float)
    action     = torch.tensor(action, dtype=torch.long)
    lod        = torch.tensor(lod, dtype=torch.long)
    reward     = torch.tensor(reward, dtype=torch.float)
    done       = torch.tensor(done, dtype=torch.float)

    q_values, deltas = model(state)
    q_values = expand_q_values(q_values, n_levels)
    if temporal_consistency or double_q:
        next_q_values_current_model = expand_q_values(model(state)[0], n_levels)
    with torch.no_grad():
        next_q_values = expand_q_values(target_model(next_state)[0], n_levels)

    n_actions = torch.pow(2,lod+1)
    expanded_actions = torch.where(action < n_actions/2, action*(n_levels-lod), (action)*(n_levels-lod) + (lod < n_levels - 1).long())

    q_values_taken = [q.gather(1, expanded_actions.unsqueeze(1)).squeeze(1) for q in q_values]

    if double_q:
        next_max_actions = [q.max(1)[1] for q in next_q_values_current_model]
        next_q_values_max = [q.gather(1, a.unsqueeze(1)).squeeze(1) for q,a in zip(next_q_values, next_max_actions)]
    else:
        next_q_values_max = [q.max(1)[0] for q in next_q_values]

    targets = [reward + gamma * next_q * (1 - done) for next_q in next_q_values_max]

    if temporal_consistency:
        next_q_values_max_current_model = [q.max(1)[0] for q in next_q_values_current_model]

    loss = torch.tensor(0.0, requires_grad=True)
    prios = torch.ones(batch_size, requires_grad=False)*1e-5
    min_lod = int(lod.min().item())
    if max_targets and not only_train_last:
        maxed_targets = [targets[0]]
        for l, t in enumerate(targets[1:]):
            maxed_targets.append(torch.max(t,maxed_targets[-1].clone()))
        targets = maxed_targets

    if only_train_last:
        loss = loss + F.smooth_l1_loss(q_values_taken[-1], targets[-1])
        prios = prios + torch.abs(q_values_taken[-1] - targets[-1])
        if temporal_consistency:
            loss = loss + F.smooth_l1_loss(next_q_values_max_current_model[-1], next_q_values_max[-1])
    else:
        masksum = torch.tensor(0.0)
        for i, (q, target) in enumerate(zip(q_values_taken, targets)):
            if temporal_consistency:
                loss = loss + F.smooth_l1_loss(next_q_values_max_current_model[i], next_q_values_max[i])
            mask = lod.le(i).float()
            if mask.sum().item() <= 32.0:
                continue
            masksum += mask.sum()
            loss = loss + (F.smooth_l1_loss(q, target.detach(), reduction="none")*mask).sum()
            prios = prios + torch.abs(q - target)*mask
        loss = loss / masksum
        prios = prios / masksum

    reg_loss = torch.tensor(0.0, requires_grad=True)
    if delta_reg_coef > 0:
        for delta in deltas:
            reg_loss = reg_loss + (delta * delta).mean() * delta_reg_coef

    replay_buffer.update_priorities(indices, prios.data.cpu().numpy())

    if lr_decay:
        for param_group in optimizer.param_groups:
            param_group['lr'] = param_group['lr']*(1 - 1/(num_frames))

    optimizer.zero_grad()
    (loss + reg_loss).backward()
    torch.nn.utils.clip_grad_norm_(model.parameters(), 10)
    optimizer.step()

    return loss, reg_loss, q_values_taken[-1].mean()


def update_target(current_model, target_model):
    target_model.load_state_dict(current_model.state_dict())


def make_env_action(action, lod, min_action=-1, max_action=1):
    this_n_action = 2**(1+lod)
    if action < this_n_action//2:
        env_action = action/this_n_action
    else:
        env_action = (action+1)/this_n_action
    env_action = env_action*(max_action - min_action) + min_action
    return env_action


def add_lod_to_state(state, lod):
    if lod_in_state:
        return np.concatenate([state, np.array(lod)], axis=None)
    else:
        return state


def eval(n_levels, n_episodes=50, frameskip=1, render=False):
    eval_rewards = []
    eval_episode_reward = 0
    state = eval_env.reset()
    for _ in range(n_episodes):
        done = False
        timeout = False
        while not (done or timeout):
            # act with highest lod for eval
            action = model.act(state, 0, n_levels-1)
            reward = 0
            for _ in range(frameskip):
                next_state, frame_reward, done, info = eval_env.step([make_env_action(action, n_levels-1)])
                if render:
                    eval_env.render()
                reward += frame_reward
                timeout = info.get("timeout", False)
                if done or timeout: break
            state = next_state
            eval_episode_reward += reward

            if done or timeout:
                state = eval_env.reset()
                eval_rewards.append(eval_episode_reward)
                eval_episode_reward = 0
    return(eval_rewards)

def get_lod(only_train_last, n_levels, frame_idx):
    if only_train_last:
        lod = n_levels - 1
        plod = 0
    else:
        plod = lod_by_frame(frame_idx)
        p_grow_lod, baselod = math.modf(plod)
        lod = int(baselod + int(np.random.random() < p_grow_lod))
    return lod, plod

def train(
    model,
    target_model,
    env,
    eval_env,
    num_frames = 75000,
    batch_size = 64,
    target_update_interval = 200,
    gamma      = 0.99,
    epsilon_start = 1.0,
    epsilon_final = 0.1,
    epsilon_decay = 50000,
    lod_growth = 25000,
    prefix = "nogas_",
    do_evals = False,
    n_levels = 1,
    n_actions = 2,
    frameskip = 1,
    train_every = 1,
    only_train_last = False,
    max_targets = True,
    double_q = True,
    temporal_consistency = False,
    delta_reg_coef = 0.0,
    lr_decay = False
):
    run_data = {
        "loss": [],
        "reg_loss": [],
        "value": [],
        "lod": [],
        "returns": [],
        "found_goal": [],
        "eval_returns": [],
        "force": [],
    }

    actions_taken = []
    episode_reward = 0

    found_any_goal = True

    state = env.reset()
    state = add_lod_to_state(state, lod_by_frame(1))
    timeout = False
    frame_idx = 1

    lod, plod = get_lod(only_train_last, n_levels, frame_idx)
    while frame_idx < num_frames:
        if found_any_goal:
            frame_idx += 1
        epsilon = epsilon_by_frame(frame_idx)
        run_data["lod"].append((frame_idx, lod))
        action = model.act(state, epsilon, lod)
        env_action = make_env_action(action, lod)

        actions_taken.append(env_action)

        reward = 0
        for _ in range(frameskip):
            next_state, frame_reward, done, info = env.step([env_action])
            next_state = add_lod_to_state(next_state, plod)
            reward += frame_reward
            if reward >= 0.5:
                found_any_goal = True
                print(f"found goal in {env.unwrapped.t} steps")
            timeout = info.get("timeout", False)
            if (done or timeout):
                break
        replay_buffer.push(state, action, lod, reward, next_state, done)

        state = next_state
        episode_reward += reward

        if done or timeout:
            state = env.reset()
            state = add_lod_to_state(state, plod)
            lod, plod = get_lod(only_train_last, n_levels, frame_idx)
            timeout = False
            run_data["returns"].append((frame_idx, episode_reward))
            run_data["found_goal"].append((frame_idx, int(reward > 0)))
            episode_reward = 0

        if len(replay_buffer) > 1000 and found_any_goal and frame_idx % train_every == 0:
            loss, reg_loss, value = compute_td_loss(batch_size,
                                                    gamma,
                                                    n_levels,
                                                    num_frames,
                                                    double_q=double_q,
                                                    temporal_consistency=temporal_consistency,
                                                    only_train_last=only_train_last,
                                                    max_targets=max_targets,
                                                    delta_reg_coef=delta_reg_coef,
                                                    lr_decay=lr_decay)
            run_data["loss"].append((frame_idx, loss.item()))
            run_data["reg_loss"].append((frame_idx, reg_loss.item()))
            run_data["value"].append((frame_idx, value.item()))

        if frame_idx % target_update_interval == 0:
            update_target(model, target_model)

        if frame_idx % 2000 == 0 and do_evals:
            print("Running evaluation")
            ev_r = eval(n_levels)
            run_data["eval_returns"].append((frame_idx, ev_r))

        if frame_idx % 1000 == 0:
            run_data["force"].append((frame_idx, np.mean(np.abs(np.array(actions_taken[-500:])))))
            print("{:8} frames | R {:.2f} (max {:.2f}) | R_eval {:.2f} (max {:.2f}) | goal_found {:.2f} | eps {:.2f} | Q {:.2f} | loss {:.6f} | reg_loss {:.6f} | Force: {:.2f} | lod {:.2f}".format(
                frame_idx,
                np.mean([x[1] for x in run_data["returns"][-10:]]),
                np.max([x[1] for x in run_data["returns"]]),
                np.mean(run_data["eval_returns"][-1][1]) if len(run_data["eval_returns"]) > 0 else np.nan,
                np.max(run_data["eval_returns"][-1][1]) if len(run_data["eval_returns"]) > 0 else np.nan,
                np.mean([x[1] for x in run_data["found_goal"][-10:]]),
                epsilon,
                np.mean([x[1] for x in run_data["value"][-100:]]),
                np.mean([x[1] for x in run_data["loss"][-100:]]),
                np.mean([x[1] for x in run_data["reg_loss"][-100:]]),
                np.mean(run_data["force"][-1][1]),
                np.mean([x[1] for x in run_data["lod"][-100:]])
                ))
    return run_data


if __name__ == "__main__":
    args = parser.parse_args()
    n_actions = 2**args.n_levels

    def epsilon_by_frame(frame_idx):
        return args.epsilon_final + max(0, (args.epsilon_start - args.epsilon_final) * min(1, (1 - (frame_idx - args.epsilon_lead_in) / args.epsilon_decay)))

    def lod_by_frame(frame_idx):
        if frame_idx < args.lod_lead_in:
            return 0
        else:
            base = float((frame_idx - args.lod_lead_in) // (args.lod_plateau + args.lod_growth))
            lin_offset = args.lod_lead_in + base * (args.lod_plateau + args.lod_growth)
            return min(base + min(1, max(0, (frame_idx - lin_offset) / args.lod_growth)) , args.n_levels - 1)

    for repeat in range(args.n_repeats):
        if args.env_id == "Acrobot":
            print("acrobot")
            env = AcrobotEnv()
            eval_env = AcrobotEnv()
        else:
            print("mcar")
            env = Continuous_MountainCarEnv()
            eval_env = Continuous_MountainCarEnv()
        model = DQN(env.observation_space.shape[0], args.n_levels, separate_models=args.separate_models, separate_heads=args.separate_heads)
        target_model  = DQN(env.observation_space.shape[0], args.n_levels, separate_models=args.separate_models, separate_heads=args.separate_heads)

        if USE_CUDA:
            model = model.cuda()

        optimizer = optim.Adam(model.parameters(), lr=0.0005, weight_decay=0.0, eps=1e-4)
        replay_buffer = NaivePrioritizedBuffer(args.buffer_size)
        run_data = train(
            model,
            target_model,
            env,
            eval_env,
            num_frames=args.num_frames,
            batch_size=args.batch_size,
            target_update_interval=args.target_update_interval,
            gamma=args.gamma,
            epsilon_start=args.epsilon_start,
            epsilon_final=args.epsilon_final,
            epsilon_decay=args.epsilon_decay,
            lod_growth=args.lod_growth,
            prefix=args.prefix,
            do_evals=args.do_evals,
            n_levels=args.n_levels,
            n_actions=n_actions,
            frameskip=args.frameskip,
            train_every=args.train_every,
            only_train_last=args.only_train_last,
            max_targets=args.max_targets,
            double_q=args.double_q,
            temporal_consistency=False,
            delta_reg_coef=args.delta_reg_coef,
            lr_decay=args.lr_decay
        )

        filename = f"{args.prefix}{repeat}.json"
        os.makedirs(os.path.dirname(filename), exist_ok=True)
        with open(filename, "w") as f:
            f.write(json.dumps(run_data))
