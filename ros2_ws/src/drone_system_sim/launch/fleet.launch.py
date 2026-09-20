from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, OpaqueFunction
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os


def build_fleet(context):
    count = int(LaunchConfiguration("drone_count").perform(context))
    count = max(1, min(count, 64))
    pkg = get_package_share_directory("drone_system_sim")
    model = os.path.join(pkg, "models", "drone", "model.sdf")

    actions = [
        Node(package="drone_system_core", executable="fleet_manager", output="screen"),
        Node(
            package="drone_system_sim",
            executable="fleet_simulator",
            output="screen",
            parameters=[{"drone_count": count}],
        ),
    ]

    for i in range(count):
        name = f"drone_{i + 1}"
        x = (i % 4) * 2.5
        y = (i // 4) * 2.5
        actions.append(
            Node(
                package="ros_gz_sim",
                executable="create",
                arguments=["-world", "fleet_world", "-file", model, "-name", name,
                           "-x", str(x), "-y", str(y), "-z", "0.4"],
                output="screen",
            )
        )
        actions.append(
            Node(
                package="ros_gz_bridge",
                executable="parameter_bridge",
                arguments=[f"/model/{name}/cmd_vel@geometry_msgs/msg/Twist]gz.msgs.Twist"],
                output="screen",
            )
        )
    return actions


def generate_launch_description():
    sim_pkg = get_package_share_directory("drone_system_sim")
    gz_pkg = get_package_share_directory("ros_gz_sim")
    world = os.path.join(sim_pkg, "worlds", "fleet_world.sdf")

    gazebo = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(os.path.join(gz_pkg, "launch", "gz_sim.launch.py")),
        launch_arguments={"gz_args": f"-r {world}"}.items(),
    )

    return LaunchDescription([
        DeclareLaunchArgument("drone_count", default_value="3"),
        gazebo,
        OpaqueFunction(function=build_fleet),
    ])
