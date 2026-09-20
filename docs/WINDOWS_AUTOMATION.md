# Intelligent Windows launcher

START-DRONE-SYSTEM.ps1 is the supported Windows entry point. START-DRONE-SYSTEM.cmd is a double-click wrapper.

## Local inventory

Before changing the computer, the launcher records a private local inventory under runtime/.

Windows inventory includes:

- Windows edition, build and architecture
- CPU topology, RAM, GPU and fixed-disk capacity
- installed applications discovered from Windows uninstall registries
- locations of Git, CMake, Python, PowerShell, winget and WSL when present

WSL inventory includes:

- Ubuntu / kernel details
- CPU and memory information
- GPU devices visible to WSL
- ROS, Gazebo, compiler and CMake status
- installed Debian packages and versions
- a boolean indicating whether XAI_API_KEY is configured

The API key value itself is never written to a report. The launcher does not upload the inventory.

## Installation policy

The bootstrap checks packages before calling apt. Already-installed ROS, Gazebo, Qt and build tools are reused. Only missing packages are sent to apt.

The Windows source tree is synchronized into the WSL Linux filesystem before building. Build, install and log caches are preserved between runs.

A source hash covers C++, interfaces, launch files, Gazebo models and UI resources. If the source hash has not changed and a valid install exists, the prior verified build is reused. If source changed, build concurrency is bounded from detected CPU and RAM and tests run after compilation unless SkipTests was explicitly requested.

## Process supervision

The full runtime is placed in a tmux session named drone-system:

- simulation window: fleet manager, simulator and Gazebo
- console window: premium Qt operations UI
- advisor window: Grok advisor when XAI_API_KEY is available
- health window: live telemetry-rate monitor

Logs are stored under runtime/logs in the Linux worktree.

## Commands

~~~powershell
# inspect without installing or launching
.\START-DRONE-SYSTEM.ps1 -Doctor

# start three drones
.\START-DRONE-SYSTEM.ps1

# start a larger fleet
.\START-DRONE-SYSTEM.ps1 -Drones 12

# run control/simulation without Gazebo rendering
.\START-DRONE-SYSTEM.ps1 -NoGazebo

# do not start the optional Grok process
.\START-DRONE-SYSTEM.ps1 -NoGrok

# stop the supervised stack
.\START-DRONE-SYSTEM.ps1 -Stop
~~~
