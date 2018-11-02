#include "aero_teleop/ps_teleop.hh"

////////////////////////////////////////////////////////////////////
aero::teleop::ps_teleop::ps_teleop
(ros::NodeHandle &nh_param) {
  mode = aero::teleop::BASIC_MODE;
  during_reset = false;
  sent_disable_msg = false;
  enable_lifter = false;
  enable_torso = false;
  enable_left_shoulder = false;
  enable_right_shoulder = false;
  enable_left_elbow = false;
  enable_right_elbow = false;
  enable_left_wrist = false;
  enable_right_wrist = false;
  enable_head = false;

  cmd_vel_pub = nh_param.advertise<geometry_msgs::Twist>("/cmd_vel", 10);

  robot.reset(new aero::interface::AeroMoveitInterface(nh_param));
  usleep(1000 * 1000);
  robot->setRobotStateToCurrentState();

  ROS_INFO("reading parameters ...");

  nh_param.param<int>("/teleop_joy/reset_pose_button", reset_pose_button, 8);
  nh_param.param<int>("/teleop_joy/switch_basic_mode", switch_basic_mode, 7);
  nh_param.param<int>("/teleop_joy/switch_joint_mode", switch_joint_mode, 6);

  nh_param.param<int>("/teleop_joy/enable_button", enable_button, 4);
  nh_param.param<int>("/teleop_joy/enable_turbo_button", enable_turbo_button, -1);
  nh_param.getParam("/teleop_joy/axis_linear", axis_linear_map);
  nh_param.getParam("/teleop_joy/scale_linear", scale_linear_map);
  nh_param.getParam("/teleop_joy/scale_linear_turbo", scale_linear_turbo_map);
  nh_param.param<int>("/teleop_joy/axis_angular", axis_angular_map[aero::teleop::YAW], 2);
  nh_param.param<double>("/teleop_joy/scale_angular", scale_angular_map[aero::teleop::YAW], 0.5);
  nh_param.param<double>("/teleop_joy/scale_angular_turbo", scale_angular_turbo_map[aero::teleop::YAW], scale_angular_map[aero::teleop::YAW]);

  nh_param.param<int>("/teleop_joy/external_command", external_command_button, 3);
  nh_param.param<int>("/teleop_joy/enable_lifter", enable_lifter_button, 5);
  nh_param.param<int>("/teleop_joy/enable_grasp_angle", enable_grasp_angle_button, 0);
  nh_param.param<int>("/teleop_joy/grasp_button/L", grasp_L_button, 2);
  nh_param.param<int>("/teleop_joy/grasp_button/R", grasp_R_button, 1);
  nh_param.getParam("/teleop_joy/lifter_axis", lifter_axis_map);
  nh_param.getParam("/teleop_joy/scale_lifter", scale_lifter_map);
  nh_param.getParam("/teleop_joy/grasp_angle_axis", grasp_angle_axis_map);
  nh_param.getParam("/teleop_joy/min_grasp_angle", min_grasp_angle_map);
  nh_param.getParam("/teleop_joy/range_grasp_angle", range_grasp_angle_map);

  nh_param.getParam("/teleop_joy/torso", torso_map);
  nh_param.getParam("/teleop_joy/scale_torso", scale_torso_map);
  nh_param.getParam("/teleop_joy/shoulder", shoulder_map);
  nh_param.getParam("/teleop_joy/scale_shoulder", scale_shoulder_map);
  nh_param.getParam("/teleop_joy/elbow", elbow_map);
  nh_param.getParam("/teleop_joy/scale_elbow", scale_elbow_map);
  nh_param.getParam("/teleop_joy/wrist", wrist_map);
  nh_param.getParam("/teleop_joy/scale_wrist", scale_wrist_map);
  nh_param.getParam("/teleop_joy/head", head_map);
  nh_param.getParam("/teleop_joy/scale_head", scale_head_map);

  sent_disable_msg = false;

  joy_sub = nh_param.subscribe<sensor_msgs::Joy>("/joy", 1, &aero::teleop::ps_teleop::joyCallback, this);

  for (auto j = aero::joint_map.begin(); j != aero::joint_map.end(); ++j) {
    if (j->first == aero::joint::waist_y || j->first == aero::joint::waist_p
        || j->first == aero::joint::neck_y || j->first == aero::joint::neck_p
        || j->first == aero::joint::l_shoulder_r || j->first == aero::joint::l_shoulder_p || j->first == aero::joint::l_shoulder_y
        || j->first == aero::joint::r_shoulder_r || j->first == aero::joint::r_shoulder_p || j->first == aero::joint::r_shoulder_y
        || j->first == aero::joint::l_elbow || j->first == aero::joint::r_elbow
        || j->first == aero::joint::l_wrist_r || j->first == aero::joint::l_wrist_p || j->first == aero::joint::l_wrist_y
        || j->first == aero::joint::r_wrist_r || j->first == aero::joint::r_wrist_p
        || j->first == aero::joint::r_wrist_y
        || j->first == aero::joint::l_hand_y || j->first == aero::joint::r_hand_y) {
      auto bounds = robot->kinematic_model->getVariableBounds(j->second);
      min_bounds[j->first] = bounds.min_position_;
      max_bounds[j->first] = bounds.max_position_;
    }
  }

  ROS_INFO("all is well");
}

////////////////////////////////////////////////////////////////////
aero::teleop::ps_teleop::~ps_teleop() {
}

////////////////////////////////////////////////////////////////////
void aero::teleop::ps_teleop::loop() {
  if (enable_lifter) {
    double x, z;
    robot->getLifter(x, z);
    ROS_INFO("lifter: (%f, %f) + d(%f, %f)", x, z, lifter_dx, lifter_dz);
    robot->setLifter(x + lifter_dx, z + lifter_dz);
    robot->sendLifter(x + lifter_dx, z + lifter_dz, 200);
  }

  if (enable_torso) {
    double y = robot->getJoint(aero::joint::waist_y);
    double goal_y = y + torso_dy;
    goal_y = std::max(goal_y, min_bounds[aero::joint::waist_y]);
    goal_y = std::min(goal_y, max_bounds[aero::joint::waist_y]);
    robot->setJoint(aero::joint::waist_y, goal_y);
    double p = robot->getJoint(aero::joint::waist_p);
    double goal_p = p + torso_dp;
    goal_p = std::max(goal_p, min_bounds[aero::joint::waist_p]);
    goal_p = std::min(goal_p, max_bounds[aero::joint::waist_p]);
    robot->setJoint(aero::joint::waist_p, goal_p);
    ROS_INFO("lifter: (0.0, %f, %f) + d(0.0, %f, %f)", p, y, torso_dp, torso_dy);
  }
  if (enable_left_shoulder) {
    double r = robot->getJoint(aero::joint::l_shoulder_r);
    double goal_r = r + left_shoulder_dr;
    goal_r = std::max(goal_r, min_bounds[aero::joint::l_shoulder_r]);
    goal_r = std::min(goal_r, max_bounds[aero::joint::l_shoulder_r]);
    robot->setJoint(aero::joint::l_shoulder_r, goal_r);
    double p = robot->getJoint(aero::joint::l_shoulder_p);
    double goal_p = p + left_shoulder_dp;
    goal_p = std::max(goal_p, min_bounds[aero::joint::l_shoulder_p]);
    goal_p = std::min(goal_p, max_bounds[aero::joint::l_shoulder_p]);
    robot->setJoint(aero::joint::l_shoulder_p, goal_p);
    double y = robot->getJoint(aero::joint::l_shoulder_y);
    double goal_y = y + left_shoulder_dy;
    goal_y = std::max(goal_y, min_bounds[aero::joint::l_shoulder_y]);
    goal_y = std::min(goal_y, max_bounds[aero::joint::l_shoulder_y]);
    robot->setJoint(aero::joint::l_shoulder_y, goal_y);
    ROS_INFO("l_shoulder: (%f, %f, %f) + d(%f, %f, %f)", r, p, y,
             left_shoulder_dr, left_shoulder_dp, left_shoulder_dy);
  }
  if (enable_right_shoulder) {
    double r = robot->getJoint(aero::joint::r_shoulder_r);
    double goal_r = r + right_shoulder_dr;
    goal_r = std::max(goal_r, min_bounds[aero::joint::r_shoulder_r]);
    goal_r = std::min(goal_r, max_bounds[aero::joint::r_shoulder_r]);
    robot->setJoint(aero::joint::r_shoulder_r, goal_r);
    double p = robot->getJoint(aero::joint::r_shoulder_p);
    double goal_p = p + right_shoulder_dp;
    goal_p = std::max(goal_p, min_bounds[aero::joint::r_shoulder_p]);
    goal_p = std::min(goal_p, max_bounds[aero::joint::r_shoulder_p]);
    robot->setJoint(aero::joint::r_shoulder_p, goal_p);
    double y = robot->getJoint(aero::joint::r_shoulder_y);
    double goal_y = y + right_shoulder_dy;
    goal_y = std::max(goal_y, min_bounds[aero::joint::r_shoulder_y]);
    goal_y = std::min(goal_y, max_bounds[aero::joint::r_shoulder_y]);
    robot->setJoint(aero::joint::r_shoulder_y, goal_y);
    ROS_INFO("r_shoulder: (%f, %f, %f) + d(%f, %f, %f)", r, p, y,
             right_shoulder_dr, right_shoulder_dp, right_shoulder_dy);
  }
  if (enable_left_elbow) {
    double p = robot->getJoint(aero::joint::l_elbow);
    double goal_p = p + left_elbow_dp;
    goal_p = std::max(goal_p, min_bounds[aero::joint::l_elbow]);
    goal_p = std::min(goal_p, max_bounds[aero::joint::l_elbow]);
    robot->setJoint(aero::joint::l_elbow, goal_p);
    double y = robot->getJoint(aero::joint::l_wrist_y);
    double goal_y = y + left_elbow_dy;
    goal_y = std::max(goal_y, min_bounds[aero::joint::l_wrist_y]);
    goal_y = std::min(goal_y, max_bounds[aero::joint::l_wrist_y]);
    robot->setJoint(aero::joint::l_wrist_y, goal_y);
    ROS_INFO("l_elbow: (0.0, %f, %f) + d(0.0, %f, %f)", p, y, left_elbow_dp, left_elbow_dy);
  }
  if (enable_right_elbow) {
    double p = robot->getJoint(aero::joint::r_elbow);
    double goal_p = p + right_elbow_dp;
    goal_p = std::max(goal_p, min_bounds[aero::joint::r_elbow]);
    goal_p = std::min(goal_p, max_bounds[aero::joint::r_elbow]);
    robot->setJoint(aero::joint::r_elbow, goal_p);
    double y = robot->getJoint(aero::joint::r_wrist_y);
    double goal_y = y + right_elbow_dy;
    goal_y = std::max(goal_y, min_bounds[aero::joint::r_wrist_y]);
    goal_y = std::min(goal_y, max_bounds[aero::joint::r_wrist_y]);
    robot->setJoint(aero::joint::r_wrist_y, goal_y);
    ROS_INFO("r_elbow: (0.0, %f, %f) + d(0.0, %f, %f)", p, y, right_elbow_dp, right_elbow_dy);
  }
  if (enable_left_wrist) {
    double p = robot->getJoint(aero::joint::l_wrist_r);
    double goal_p = p + left_wrist_dp;
    goal_p = std::max(goal_p, min_bounds[aero::joint::l_wrist_r]);
    goal_p = std::min(goal_p, max_bounds[aero::joint::l_wrist_r]);
    robot->setJoint(aero::joint::l_wrist_r, goal_p);
    double y = robot->getJoint(aero::joint::l_hand_y);
    double goal_y = y + left_wrist_dy;
    goal_y = std::max(goal_y, min_bounds[aero::joint::l_hand_y]);
    goal_y = std::min(goal_y, max_bounds[aero::joint::l_hand_y]);
    robot->setJoint(aero::joint::l_hand_y, goal_y);
    ROS_INFO("l_wrist: (0.0, %f, %f) + d(0.0, %f, %f)", p, y, left_wrist_dp, left_wrist_dy);
  }
  if (enable_right_wrist) {
    double p = robot->getJoint(aero::joint::r_wrist_r);
    double goal_p = p + right_wrist_dp;
    goal_p = std::max(goal_p, min_bounds[aero::joint::r_wrist_r]);
    goal_p = std::min(goal_p, max_bounds[aero::joint::r_wrist_r]);
    robot->setJoint(aero::joint::r_wrist_r, goal_p);
    double y = robot->getJoint(aero::joint::r_hand_y);
    double goal_y = y + right_wrist_dy;
    goal_y = std::max(goal_y, min_bounds[aero::joint::r_hand_y]);
    goal_y = std::min(goal_y, max_bounds[aero::joint::r_hand_y]);
    robot->setJoint(aero::joint::r_hand_y, goal_y);
    ROS_INFO("r_wrist: (0.0, %f, %f) + d(0.0, %f, %f)", p, y, right_wrist_dp, right_wrist_dy);
  }
  if (enable_head) {
    double y = robot->getJoint(aero::joint::neck_y);
    double goal_y = y + head_dy;
    goal_y = std::max(goal_y, min_bounds[aero::joint::neck_y]);
    goal_y = std::min(goal_y, max_bounds[aero::joint::neck_y]);
    robot->setJoint(aero::joint::neck_y, goal_y);
    double p = robot->getJoint(aero::joint::neck_p);
    double goal_p = p + head_dp;
    goal_p = std::max(goal_p, min_bounds[aero::joint::neck_p]);
    goal_p = std::min(goal_p, max_bounds[aero::joint::neck_p]);
    robot->setJoint(aero::joint::neck_p, goal_p);
    ROS_INFO("lifter: (0.0, %f, %f) + d(0.0, %f, %f)", p, y, head_dp, head_dy);
  }

  if (enable_torso || enable_head
      || enable_right_shoulder || enable_left_shoulder
      || enable_right_elbow || enable_left_elbow
      || enable_right_wrist || enable_left_wrist)
    robot->sendModelAngles(200, aero::ikrange::upperbody);
  else if (!enable_lifter)
    robot->setRobotStateToCurrentState();
}

////////////////////////////////////////////////////////////////////
void aero::teleop::ps_teleop::joyCallback
(const sensor_msgs::Joy::ConstPtr &joy_msg) {
  if (joy_msg->buttons[switch_basic_mode])
    mode = aero::teleop::BASIC_MODE;
  else if (joy_msg->buttons[switch_joint_mode])
    mode = aero::teleop::JOINT_MODE;

  // other commands are not allowed during reset
  if (during_reset && (ros::Time::now() - reset_start_time).toSec() < 5.0)
    return;
  during_reset = false;

  if (external_command_button >= 0 && joy_msg->buttons[reset_pose_button]
      && joy_msg->buttons[external_command_button])
    system("spd-say -i -50 -r -60 -p 50 -m all -t child_female \"$(ifconfig | grep 10.81 | awk '{print $2}' | cut -d: -f2)\"");

  if (joy_msg->buttons[reset_pose_button] &&
      !(mode != aero::teleop::JOINT_MODE && // forbids basic_mode + external
        external_command_button >= 0 &&
        joy_msg->buttons[external_command_button])) {
    robot->setPoseVariables(aero::pose::reset_manip);
    if (joy_msg->buttons[enable_lifter_button]) { // also reset lifter
      robot->setLifter(0.0, 0.0);
      robot->sendModelAngles(5000, aero::ikrange::wholebody);
    } else {
      robot->sendModelAngles(5000, aero::ikrange::upperbody);
    }
    during_reset = true;
    reset_start_time = ros::Time::now();
  }

  if (mode == aero::teleop::JOINT_MODE) {
    enable_lifter = false;
    jointMode(joy_msg);
  } else {
    enable_torso = false;
    enable_left_shoulder = false;
    enable_right_shoulder = false;
    enable_left_elbow = false;
    enable_right_elbow = false;
    enable_left_wrist = false;
    enable_right_wrist = false;
    enable_head = false;
    basicMode(joy_msg);
  }
}

////////////////////////////////////////////////////////////////////
void aero::teleop::ps_teleop::basicMode
(const sensor_msgs::Joy::ConstPtr &joy_msg) {
  // check for external disables

  if (external_command_button >= 0 && joy_msg->buttons[external_command_button])
    return; // all commands are disabled when external command button is pressed

  // handle twist

  geometry_msgs::Twist cmd_vel_msg;

  if (enable_turbo_button >= 0 && joy_msg->buttons[enable_turbo_button]) {
    if (axis_linear_map.find(aero::teleop::X) != axis_linear_map.end())
      cmd_vel_msg.linear.x = joy_msg->axes[axis_linear_map[aero::teleop::X]] * scale_linear_turbo_map[aero::teleop::X];
    if (axis_linear_map.find(aero::teleop::Y) != axis_linear_map.end())
      cmd_vel_msg.linear.y = joy_msg->axes[axis_linear_map[aero::teleop::Y]] * scale_linear_turbo_map[aero::teleop::Y];
    if (axis_linear_map.find(aero::teleop::Z) != axis_linear_map.end())
      cmd_vel_msg.linear.z = joy_msg->axes[axis_linear_map[aero::teleop::Z]] * scale_linear_turbo_map[aero::teleop::Z];
    if (axis_angular_map.find(aero::teleop::YAW) != axis_angular_map.end())
      cmd_vel_msg.angular.z = joy_msg->axes[axis_angular_map[aero::teleop::YAW]] * scale_angular_turbo_map[aero::teleop::YAW];

    cmd_vel_pub.publish(cmd_vel_msg);
    sent_disable_msg = false;
  }
  else if (joy_msg->buttons[enable_button]) {
    if (axis_linear_map.find(aero::teleop::X) != axis_linear_map.end())
      cmd_vel_msg.linear.x = joy_msg->axes[axis_linear_map[aero::teleop::X]] * scale_linear_map[aero::teleop::X];
    if (axis_linear_map.find(aero::teleop::Y) != axis_linear_map.end())
      cmd_vel_msg.linear.y = joy_msg->axes[axis_linear_map[aero::teleop::Y]] * scale_linear_map[aero::teleop::Y];
    if (axis_linear_map.find(aero::teleop::Z) != axis_linear_map.end())
      cmd_vel_msg.linear.z = joy_msg->axes[axis_linear_map[aero::teleop::Z]] * scale_linear_map[aero::teleop::Z];
    if (axis_angular_map.find(aero::teleop::YAW) != axis_angular_map.end())
      cmd_vel_msg.angular.z = joy_msg->axes[axis_angular_map[aero::teleop::YAW]] * scale_angular_map[aero::teleop::YAW];

    cmd_vel_pub.publish(cmd_vel_msg);
    sent_disable_msg = false;
  }
  else {
    // When enable button is released, immediately send a single no-motion command
    // in order to stop the robot.
    if (!sent_disable_msg) {
      cmd_vel_pub.publish(cmd_vel_msg);
      sent_disable_msg = true;
    }
  }

  // handle other basics

  if (joy_msg->buttons[enable_lifter_button]) {
    enable_lifter = true;
    lifter_dx = joy_msg->axes[lifter_axis_map[aero::teleop::X]] * scale_lifter_map[aero::teleop::X];
    lifter_dz = joy_msg->axes[lifter_axis_map[aero::teleop::Z]] * scale_lifter_map[aero::teleop::Z];
  } else {
    enable_lifter = false;
  }

  if (joy_msg->buttons[enable_grasp_angle_button]) {
    if (!in_grasp_mode) {
      robot->openHand(aero::arm::larm);
      robot->openHand(aero::arm::rarm);
      in_grasp_mode = true;
    }
  } else {
    in_grasp_mode = false;
  }

  if (joy_msg->buttons[grasp_L_button])
    robot->sendGrasp(aero::arm::larm, 100);
  if (joy_msg->buttons[grasp_R_button])
    robot->sendGrasp(aero::arm::rarm, 100);
}

////////////////////////////////////////////////////////////////////
void aero::teleop::ps_teleop::jointMode
(const sensor_msgs::Joy::ConstPtr &joy_msg) {
  // torso

  if (joy_msg->buttons[torso_map[aero::teleop::FLAG]]) {
    enable_torso = true;
    if (joy_msg->buttons[torso_map[aero::teleop::YAW_PLUS]])
      torso_dy = scale_torso_map[aero::teleop::YAW];
    else if (joy_msg->buttons[torso_map[aero::teleop::YAW_MINUS]])
      torso_dy = -scale_torso_map[aero::teleop::YAW];
    else
      torso_dy = 0;
    if (joy_msg->buttons[torso_map[aero::teleop::PITCH_PLUS]])
      torso_dp = scale_torso_map[aero::teleop::PITCH];
    else if (joy_msg->buttons[torso_map[aero::teleop::PITCH_MINUS]])
      torso_dp = -scale_torso_map[aero::teleop::PITCH];
    else
      torso_dp = 0;
  } else {
    enable_torso = false;
  }

  // shoulder

  if (joy_msg->buttons[shoulder_map[aero::teleop::LEFT_FLAG]]) {
    enable_left_shoulder = true;
    if (joy_msg->buttons[shoulder_map[aero::teleop::ROLL_PLUS]])
      left_shoulder_dr = scale_shoulder_map[aero::teleop::ROLL];
    else if (joy_msg->buttons[shoulder_map[aero::teleop::ROLL_MINUS]])
      left_shoulder_dr = -scale_shoulder_map[aero::teleop::ROLL];
    else
      left_shoulder_dr = 0;
    if (joy_msg->buttons[shoulder_map[aero::teleop::PITCH_PLUS]])
      left_shoulder_dp = scale_shoulder_map[aero::teleop::PITCH];
    else if (joy_msg->buttons[shoulder_map[aero::teleop::PITCH_MINUS]])
      left_shoulder_dp = -scale_shoulder_map[aero::teleop::PITCH];
    else
      left_shoulder_dp = 0;
    left_shoulder_dy = joy_msg->axes[shoulder_map[aero::teleop::YAW]] * scale_shoulder_map[aero::teleop::YAW];
  } else {
    enable_left_shoulder = false;
  }

  if (joy_msg->buttons[shoulder_map[aero::teleop::RIGHT_FLAG]]) {
    enable_right_shoulder = true;
    if (joy_msg->buttons[shoulder_map[aero::teleop::ROLL_PLUS]])
      right_shoulder_dr = scale_shoulder_map[aero::teleop::ROLL];
    else if (joy_msg->buttons[shoulder_map[aero::teleop::ROLL_MINUS]])
      right_shoulder_dr = -scale_shoulder_map[aero::teleop::ROLL];
    else
      right_shoulder_dr = 0;
    if (joy_msg->buttons[shoulder_map[aero::teleop::PITCH_PLUS]])
      right_shoulder_dp = scale_shoulder_map[aero::teleop::PITCH];
    else if (joy_msg->buttons[shoulder_map[aero::teleop::PITCH_MINUS]])
      right_shoulder_dp = -scale_shoulder_map[aero::teleop::PITCH];
    else
      right_shoulder_dp = 0;
    right_shoulder_dy = joy_msg->axes[shoulder_map[aero::teleop::YAW]] * scale_shoulder_map[aero::teleop::YAW];
  } else {
    enable_right_shoulder = false;
  }

  // elbow

  if (joy_msg->buttons[elbow_map[aero::teleop::RIGHT_FLAG]]) {
    enable_right_elbow = true;
    if (joy_msg->buttons[elbow_map[aero::teleop::YAW_PLUS]])
      right_elbow_dy = scale_elbow_map[aero::teleop::YAW];
    else if (joy_msg->buttons[elbow_map[aero::teleop::YAW_MINUS]])
      right_elbow_dy = -scale_elbow_map[aero::teleop::YAW];
    else
      right_elbow_dy = 0;
    if (joy_msg->buttons[elbow_map[aero::teleop::PITCH_PLUS]])
      right_elbow_dp = scale_elbow_map[aero::teleop::PITCH];
    else if (joy_msg->buttons[elbow_map[aero::teleop::PITCH_MINUS]])
      right_elbow_dp = -scale_elbow_map[aero::teleop::PITCH];
    else
      right_elbow_dp = 0;
  } else {
    enable_right_elbow = false;
  }

  if (joy_msg->buttons[elbow_map[aero::teleop::LEFT_FLAG]]) {
    enable_left_elbow = true;
    if (joy_msg->buttons[elbow_map[aero::teleop::YAW_PLUS]])
      left_elbow_dy = scale_elbow_map[aero::teleop::YAW];
    else if (joy_msg->buttons[elbow_map[aero::teleop::YAW_MINUS]])
      left_elbow_dy = -scale_elbow_map[aero::teleop::YAW];
    else
      left_elbow_dy = 0;
    if (joy_msg->buttons[elbow_map[aero::teleop::PITCH_PLUS]])
      left_elbow_dp = scale_elbow_map[aero::teleop::PITCH];
    else if (joy_msg->buttons[elbow_map[aero::teleop::PITCH_MINUS]])
      left_elbow_dp = -scale_elbow_map[aero::teleop::PITCH];
    else
      left_elbow_dp = 0;
  } else {
    enable_left_elbow = false;
  }  

  // wrist

  if (joy_msg->buttons[wrist_map[aero::teleop::RIGHT_FLAG]]) {
    enable_right_wrist = true;
    if (joy_msg->buttons[wrist_map[aero::teleop::YAW_PLUS]])
      right_wrist_dy = scale_wrist_map[aero::teleop::YAW];
    else if (joy_msg->buttons[wrist_map[aero::teleop::YAW_MINUS]])
      right_wrist_dy = -scale_wrist_map[aero::teleop::YAW];
    else
      right_wrist_dy = 0;
    if (joy_msg->buttons[wrist_map[aero::teleop::PITCH_PLUS]])
      right_wrist_dp = scale_wrist_map[aero::teleop::PITCH];
    else if (joy_msg->buttons[wrist_map[aero::teleop::PITCH_MINUS]])
      right_wrist_dp = -scale_wrist_map[aero::teleop::PITCH];
    else
      right_wrist_dp = 0;
  } else {
    enable_right_wrist = false;
  }

  if (joy_msg->buttons[wrist_map[aero::teleop::LEFT_FLAG]]) {
    enable_left_wrist = true;
    if (joy_msg->buttons[wrist_map[aero::teleop::YAW_PLUS]])
      left_wrist_dy = scale_wrist_map[aero::teleop::YAW];
    else if (joy_msg->buttons[wrist_map[aero::teleop::YAW_MINUS]])
      left_wrist_dy = -scale_wrist_map[aero::teleop::YAW];
    else
      left_wrist_dy = 0;
    if (joy_msg->buttons[wrist_map[aero::teleop::PITCH_PLUS]])
      left_wrist_dp = scale_wrist_map[aero::teleop::PITCH];
    else if (joy_msg->buttons[wrist_map[aero::teleop::PITCH_MINUS]])
      left_wrist_dp = -scale_wrist_map[aero::teleop::PITCH];
    else
      left_wrist_dp = 0;
  } else {
    enable_left_wrist = false;
  }  

  // head

  if (joy_msg->buttons[head_map[aero::teleop::FLAG]]) {
    enable_head = true;
    if (joy_msg->buttons[head_map[aero::teleop::YAW_PLUS]])
      head_dy = scale_head_map[aero::teleop::YAW];
    else if (joy_msg->buttons[head_map[aero::teleop::YAW_MINUS]])
      head_dy = -scale_head_map[aero::teleop::YAW];
    else
      head_dy = 0;
    if (joy_msg->buttons[head_map[aero::teleop::PITCH_PLUS]])
      head_dp = scale_head_map[aero::teleop::PITCH];
    else if (joy_msg->buttons[head_map[aero::teleop::PITCH_MINUS]])
      head_dp = -scale_head_map[aero::teleop::PITCH];
    else
      head_dp = 0;
  } else {
    enable_head = false;
  }
}
