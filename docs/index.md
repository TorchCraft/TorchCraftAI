# CherryPi: A modern and ML-friendly full game BroodWar bot

## Introduction
This is CherryPi, a StarCraft BroodWar(TM) bot built with [TorchCraft](https://github.com/TorchCraft/TorchCraft).
This page gives a brief overview of the bot design and some main concepts that are useful to know about when working with the code-base.

## Design and Main Concepts
There are a handful of central classes and concepts that could benefit from having a coherent joint explanation.
Here it comes:

- [Player](\ref cherrypi::Player) uses a TorchCraft client to connect to a TorchCraft server and play the game.
  The behavior of the bot is defined by a set of [Modules](\ref cherrypi::Module) operating in a strict order on the [game state](\ref cherrypi::Module).

- A [Module](\ref cherrypi::Module) implements a specific subset of the bot's behavior.
  Common examples would be the production of units, placement of buildings, managing of resource gather or micro-management for battles.
  [Modules](\ref cherrypi::Module) should strive to has as little internal state as possible and rather use the [Blackboard](\ref cherrypi::Blackboard) when possible.

- [Modules](\ref cherrypi::Module) act upon information available on the [Blackboard](\ref cherrypi::Blackboard) and the [State](\ref cherrypi::State).
  Both Blackboard and State have a per-game lifetime.
  The main means of communication are [UPCTuples](\ref cherrypi::UPCTuple), which define distributions over units, positions and a handful of abstract [commands](\ref cherrypi::Command).
  [Modules](\ref cherrypi::Module) consume [UPCTuples](\ref cherrypi::UPCTuple) and post new ones to the [Blackboard](\ref cherrypi::Blackboard) with refined distributions.
  [UPCTuples](\ref cherrypi::UPCTuple) that are actionable (i.e. the distributions are sharp and point to a specific unit, position and command with a probability of 1) are translated into game commands by the [UPCToCommandModule](\ref cherrypi::UPCToCommandModule).
  The [Blackboard](\ref cherrypi::Blackboard) will assign unique IDs to all [UPCTuples](\ref cherrypi::UPCTuple) that are posted, and it also keeps track of the [Modules](\ref cherrypi::Module) that posted and consumed them.
  Besides [UPCTuples](\ref cherrypi::UPCTuple), [Modules](\ref cherrypi::Module) can use two further means of communication and storage:

- [Tasks](\ref cherrypi::Task) represent durative actions initiated by the consumption of a [UPCTuple](\ref cherrypi::UPCTuple) and have resources attached to them: a set of units and planned resource usage (ore, minerals, supply) that is not yet accounted for in the raw game state.
  [Tasks](\ref cherrypi::Task) are posted to the [Blackboard](\ref cherrypi::Blackboard) as well.
  They have an owner (a [Module](\ref cherrypi::Module)) and refer to a single [UPCTuple](\ref cherrypi::UPCTuple) which spawned them.
  [Tasks](\ref cherrypi::Task) are updated before [Modules](\ref cherrypi::Module) are being run.
  However, a [Task](\ref cherrypi::Task)'s update function should not perform any heavy lifting, but instead incorporate new information from the State in order to update internal data or the their [status](\ref cherrypi::TaskStatus).

- An important aspect of [Tasks](\ref cherrypi::Task) is the allocation of units.
  Units returned by a [Task](\ref cherrypi::Task)'s [units()](\ref cherrypi::Task::units) function are considered to be allocated.
  However, this allocation is non-binding!
  [Modules](\ref cherrypi::Module) are free to spawn tasks that include units that are already allocated, provided a corresponding [UPCTuple](\ref cherrypi::UPCTuple) contains a non-zero probability for them.
  Hence, [Modules](\ref cherrypi::Module) must take care to remove re-allocated units from their tasks, and, if the task can no longer be completed due to the lack of units, mark them as [failed](\ref cherrypi::TaskStatus::Failure).

- [ProxyTasks](\ref cherrypi::ProxyTask) can be used to track other tasks.
  They are constructed with a target UPC ID (returned from [Blackboard::postUPC()](\ref cherrypi::Blackboard::postUPC)).
  [ProxyTasks](\ref cherrypi::ProxyTask) scan the [Blackboard](\ref cherrypi::Blackboard) for a [Task](\ref cherrypi::Task) associated to their target UPC ID and, if they found a corresponding [Task](\ref cherrypi::Task), will mirror its status.
  The [Blackboard](\ref cherrypi::Blackboard) makes sure that [Tasks](\ref cherrypi::Task) are updated
  in reverse order of their construction so that a proxy should always reflect
  the most recent status of its target [Task](\ref cherrypi::Task).

- [Controllers](\ref cherrypi::ControllerBase) are used to implement actual unit
  control. They are closely tied to [Tasks](\ref cherrypi::Task) and cover common
  control patterns via [Controller](\ref cherrypi::Controller) and
  [SharedController](\ref cherrypi::SharedController).

- [Tasks](\ref cherrypi::Task) provide a good way for [Modules](\ref cherrypi::Module) to store data for an action that they are currently performing.
  In order to do so, [Modules](\ref cherrypi::Module) can implement custom [Tasks](\ref cherrypi::Task) by inheriting from
  [Task](\ref cherrypi::Task) or [ProxyTask](\ref cherrypi::ProxyTask).

- [Trackers](\ref cherrypi::Tracker) are general, light-weight objects that track the execution of specific commands in the game.
  Good use-cases are unit movement or the construction of a building.
