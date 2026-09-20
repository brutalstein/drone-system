uint8 HOLD=0
uint8 TAKEOFF=1
uint8 LAND=2
uint8 RETURN_HOME=3
uint8 VELOCITY=4
uint8 EMERGENCY_STOP=5
uint8 GOTO_POSITION=6

string drone_id
uint64 sequence
builtin_interfaces/Time issued_at
uint32 ttl_ms
uint8 mode
geometry_msgs/Vector3 linear
float64 yaw_rate
float64 takeoff_altitude_m
geometry_msgs/Point target_position
float64 max_speed_mps
