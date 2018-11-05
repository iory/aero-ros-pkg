/// @author Sasabuchi Kazuhiro, Shintaro Hori, Hiroaki Yaguchi

#include "AeroMoveBaseRH.hh"

using namespace aero;
using namespace navigation;

//////////////////////////////////////////////////
/// @brief constructor
/// @param _nh ROS Node Handle
AeroMoveBase::AeroMoveBase(const ros::NodeHandle& _nh,
                           aero_robot_hardware::AeroRobotHW *_in_hw) :
  nh_(_nh),
  vx_(0), vy_(0), vth_(0), x_(0), y_(0), th_(0), base_spinner_(1, &base_queue_), base_config_()
{
  prev_cmd_.linear.x = 0;
  prev_cmd_.linear.y = 0;
  prev_cmd_.angular.z = 0;

  base_config_.Init(ros_rate_,
                    odom_rate_,
                    safe_rate_,
                    safe_duration_,
                    num_of_wheels_,
                    wheel_names_);

  hw_ = _in_hw;

  servo_ = false;

  current_time_ = ros::Time::now();
  last_time_ = current_time_;

  base_ops_ = ros::SubscribeOptions::create<geometry_msgs::Twist >
    ( "cmd_vel", 2,
      boost::bind(&AeroMoveBase::CmdVelCallback, this, _1),
      ros::VoidPtr(), &base_queue_);

  cmd_vel_sub_ = nh_.subscribe(base_ops_);

  // for safety check
  ros::TimerOptions tmopt(ros::Duration(safe_rate_),
                          boost::bind(&AeroMoveBase::SafetyCheckCallback,
                                      this, _1),
                          &base_queue_);
  safe_timer_ = _nh.createTimer(tmopt);

  base_spinner_.start();

  // for odometory
  odom_pub_ = nh_.advertise<nav_msgs::Odometry>("odom", 1);

  odom_timer_ = nh_.createTimer(ros::Duration(odom_rate_),
                                &AeroMoveBase::CalculateOdometry, this);
}

//////////////////////////////////////////////////
/// @brief destructor
AeroMoveBase::~AeroMoveBase()
{
}

//////////////////////////////////////////////////
/// @brief control with cmd_vel
void AeroMoveBase::CmdVelCallback(const geometry_msgs::TwistConstPtr& _cmd_vel)
{
  ros::Time now = ros::Time::now();
  ROS_DEBUG("cmd_vel: %f %f %f",
            _cmd_vel->linear.x,
            _cmd_vel->linear.y,
            _cmd_vel->angular.z);

  if (base_mtx_.try_lock()) {
    if (_cmd_vel->linear.x == 0.0 &&
        _cmd_vel->linear.y == 0.0 &&
        _cmd_vel->angular.z == 0.0) {
      vx_ = vy_ = vth_ = 0.0;
    } else {
    double dt = (now - time_stamp_).toSec();
    double acc_x = (_cmd_vel->linear.x  - vx_)  / dt;
    double acc_y = (_cmd_vel->linear.y  - vy_)  / dt;
    double acc_z = (_cmd_vel->angular.z - vth_) / dt;

    ROS_DEBUG("vel_acc: %f %f %f", acc_x, acc_y, acc_z);
#define MAX_ACC_X 3.0
#define MAX_ACC_Y 3.0
#define MAX_ACC_Z 3.0
    if (acc_x >   MAX_ACC_X) acc_x =   MAX_ACC_X;
    if (acc_x < - MAX_ACC_X) acc_x = - MAX_ACC_X;
    if (acc_y >   MAX_ACC_Y) acc_y =   MAX_ACC_Y;
    if (acc_y < - MAX_ACC_Y) acc_y = - MAX_ACC_Y;
    if (acc_z >   MAX_ACC_Z) acc_z =   MAX_ACC_Z;
    if (acc_z < - MAX_ACC_Z) acc_z = - MAX_ACC_Z;

    vx_  += acc_x * dt;
    vy_  += acc_y * dt;
    vth_ += acc_z * dt;
    }
    ROS_DEBUG("act_vel: %f %f %f", vx_, vy_, vth_);

    //check servo state
    if ( !servo_ ) {
      servo_ = true;
      hw_->startWheelServo();
    }

    std::vector<int16_t> int_vel(num_of_wheels_);

    // convert velocity to wheel
    base_config_.VelocityToWheel(vx_, vy_, vth_, int_vel);

    hw_->writeWheel(wheel_names_, int_vel, ros_rate_);

    //update time_stamp_
    time_stamp_ = now;
    prev_cmd_ = *_cmd_vel;

    usleep(10*1000); // 10ms // not need
    base_mtx_.unlock();
  } else {
    ROS_WARN("cmd_vel comes before sending pervious message");
  }
}

//////////////////////////////////////////////////
/// @brief safety stopper when msg is not reached
///  for more than `safe_duration_` [s]
void AeroMoveBase::SafetyCheckCallback(const ros::TimerEvent& _event)
{
  boost::mutex::scoped_lock lk(base_mtx_);
  if((ros::Time::now() - time_stamp_).toSec() >= safe_duration_ && servo_) {
    std::vector<int16_t> int_vel(num_of_wheels_);
    vx_ = vy_ = vth_ = 0.0;
    ROS_WARN("Base: safety stop");
    for (size_t i = 0; i < num_of_wheels_; i++) {
      int_vel[i] = 0;
    }
    hw_->writeWheel(wheel_names_, int_vel, ros_rate_);

    servo_ = false;
    hw_->stopWheelServo();
  }
}

//////////////////////////////////////////////////
/// @brief odometry publisher
void AeroMoveBase::CalculateOdometry(const ros::TimerEvent& _event)
{
  current_time_ = ros::Time::now();

  double dt, delta_x, delta_y, delta_th;
  dt = (current_time_ - last_time_).toSec();
  delta_x  = (vx_ * cos(th_) - vy_ * sin(th_)) * dt;
  delta_y  = (vx_ * sin(th_) + vy_ * cos(th_)) * dt;
  delta_th = vth_ * dt;

  x_  += delta_x;
  y_  += delta_y;
  th_ += delta_th;

  // odometry is 6DOF so we'll need a quaternion created from yaw
  geometry_msgs::Quaternion odom_quat
      = tf::createQuaternionMsgFromYaw(th_);

  // first, we'll publish the transform over tf
  geometry_msgs::TransformStamped odom_trans;
  odom_trans.header.stamp = current_time_;
  odom_trans.header.frame_id = "odom";
  odom_trans.child_frame_id  = "base_link";

  odom_trans.transform.translation.x = x_;
  odom_trans.transform.translation.y = y_;
  odom_trans.transform.translation.z = 0.0;
  odom_trans.transform.rotation = odom_quat;

  // send the transform
  odom_broadcaster_.sendTransform(odom_trans);

  // next, we'll publish the odometry message over ROS
  nav_msgs::Odometry odom;
  odom.header.stamp = current_time_;
  odom.header.frame_id = "odom";

  // set the position
  odom.pose.pose.position.x = x_;
  odom.pose.pose.position.y = y_;
  odom.pose.pose.position.z = 0.0;
  odom.pose.pose.orientation = odom_quat;

  // set the velocity
  odom.child_frame_id = "base_link";
  odom.twist.twist.linear.x  = vx_;
  odom.twist.twist.linear.y  = vy_;
  odom.twist.twist.angular.z = vth_;

  // publish the message
  odom_pub_.publish(odom);

  last_time_ = current_time_;
}
