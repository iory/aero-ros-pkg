/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2018, Yohei Kakiuchi (JSK lab.)
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the Open Source Robotics Foundation
 *     nor the names of its contributors may be
 *     used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/*
 Author: Yohei Kakiuchi
*/

#include <aero_ros_controller/RobotInterface.hh>

#include <limits>

using namespace robot_interface;

//// TrajectoryBase ////

bool TrajectoryBase::convertToAngleVector(const joint_angle_map &_jmap, angle_vector &_av)
{
  bool result = false;
  if (_av.size() != joint_list_.size()) {
    _av.resize(joint_list_.size());
  }
  for(int i = 0; i < joint_list_.size(); i++) {
    auto it = _jmap.find(joint_list_[i]);
    if (it != _jmap.end()) {
      _av[i] = it->second;
      result = true;
    } else {
      // DO NOTHING
      // ROS_ERROR("not found %s", joint_list_[i].c_str());
      // _av[i] = numeric_limits<double>::quiet_NaN();
    }
  }
  // return true if at least one value has changed
  return result;
}

bool TrajectoryBase::convertToAngleVector(const std::vector < std::string> &_names,
                                          const std::vector< double> &_positions,
                                          angle_vector &_av)
{
  // _names and _positions should have same length
  joint_angle_map jmap;
  int size = std::min(_names.size(), _positions.size());
  if(size == 0) return false;
  for(int i = 0; i < size; i++) {
    jmap[_names[i]] = _positions[i];
  }
  return convertToAngleVector(jmap, _av);
}

bool TrajectoryBase::convertToMap(const angle_vector &_av, joint_angle_map &_jmap)
{
  int size = std::min(_av.size(), joint_list_.size());
  if(size == 0) return false;
  _jmap.clear();
  for(int i = 0; i < size; i++) {
    _jmap[joint_list_[i]] = _av[i];
  }
  return true;
}

bool TrajectoryBase::sendAngles(const joint_angle_map &_jmap,
                                const double _tm, const ros::Time &_start)
{
  angle_vector av;
  reference_vector(av);
  if(convertToAngleVector(_jmap, av)) {
    send_angle_vector(av, _tm, _start);
  }
}

bool TrajectoryBase::sendAngles(const std::vector < std::string> &_names,
                                const std::vector< double> &_positions,
                                const double _tm, const ros::Time &_start)
{
  angle_vector av;
  reference_vector(av);
  if(convertToAngleVector(_names, _positions, av)) {
    send_angle_vector(av, _tm, _start);
  }
}

void TrajectoryBase::send_angle_vector(const angle_vector &_av, const double _tm)
{
  ros::Time now = ros::Time::now() + ros::Duration(start_offset_);
  send_angle_vector(_av, _tm, now);
}

void TrajectoryBase::send_angle_vector_sequence(const angle_vector_sequence &_av_seq, const time_vector &_tm_seq)
{
  ros::Time now = ros::Time::now() + ros::Duration(start_offset_);
  send_angle_vector_sequence(_av_seq, _tm_seq, now);
}

void TrajectoryBase::reference_vector(angle_vector &_ref)
{
  joint_angle_map map;
  this->getReferencePositions(map);
  this->convertToAngleVector(map, _ref);
}

void TrajectoryBase::potentio_vector(angle_vector &_ref)
{
  joint_angle_map map;
  this->getActualPositions(map);
  this->convertToAngleVector(map, _ref);
}

//// TrajectoryClient ////

TrajectoryClient::TrajectoryClient(ros::NodeHandle &_nh,
                                   const std::string &_act_name,
                                   const std::string &_state_name,
                                   const std::vector<std::string > &_jnames) :
  SimpleActionClient<control_msgs::FollowJointTrajectoryAction>(_nh, _act_name), TrajectoryBase(_jnames), sending_goal_(false), updated_state_(false)
{
  joint_list_ = _jnames;
  ros::Duration timeout(10);
  if(!this->waitForServer(timeout)) {
    ROS_ERROR("timeout for waiting %s%s", _nh.getNamespace().c_str(), _act_name.c_str());
    return;
  }
  if (true) { // USE spinner
    state_spinner_.reset(new ros::AsyncSpinner(1, &state_queue_));
    ros::SubscribeOptions sub_ops = ros::SubscribeOptions::create< control_msgs::JointTrajectoryControllerState >
      ( _state_name, 10,
        boost::bind(&TrajectoryClient::StateCallback_, this, _1),
        ros::VoidPtr(), &state_queue_);
    state_sub_ = _nh.subscribe(sub_ops);
    state_spinner_->start();
  } else {
    state_sub_ = _nh.subscribe
      (_state_name, 10, &TrajectoryClient::StateCallback_, this);
  }
  // wait first state !!!
  while(!updated_state_) {
    ros::Duration d(0.1);
    d.sleep();
  }
}

TrajectoryClient::~TrajectoryClient()
{
  if(!!state_spinner_) {
    state_spinner_->stop();
    {
      boost::mutex::scoped_lock lock(state_mtx_);
      state_sub_.shutdown();
    }
  }
  //ROS_WARN("~ %s", name_.c_str());
}

void TrajectoryClient::getReferencePositions( joint_angle_map &_map)
{
  boost::mutex::scoped_lock lock(state_mtx_);
  for(int i = 0; i < current_state_.desired.positions.size(); i++) {
    _map[current_state_.joint_names[i]]
      = current_state_.desired.positions[i];
  }
}

void TrajectoryClient::getActualPositions( joint_angle_map &_map)
{
  boost::mutex::scoped_lock lock(state_mtx_);
  for(int i = 0; i < current_state_.actual.positions.size(); i++) {
    _map[current_state_.joint_names[i]]
      = current_state_.actual.positions[i];
  }
}

void TrajectoryClient::send_angle_vector(const angle_vector &_av, const double _tm, const ros::Time &_start)
{
  if (_av.size() != joint_list_.size()) {
    ROS_ERROR("er1"); //TODO
    return;
  }
  int jsize = joint_list_.size();
  control_msgs::FollowJointTrajectoryGoal goal;
  goal.trajectory.header.stamp = _start;
  goal.trajectory.joint_names = joint_list_;
  goal.trajectory.points.resize(1);
  goal.trajectory.points[0].positions.resize(jsize);
  for(int j = 0; j < jsize; j++) {
    goal.trajectory.points[0].positions[j] = _av[j];
  }
  //goal.trajectory.points[0].velocities.resize();
  //goal.trajectory.points[0].accelerations.resize();
  //goal.trajectory.points[0].effort.resize();
  goal.trajectory.points[0].time_from_start = ros::Duration(_tm);
  goal.path_tolerance.resize(0);
  goal.goal_tolerance.resize(0);
  goal.goal_time_tolerance = ros::Duration(goal_time_tolerance_);
  //this->sendGoal(goal);
  sending_goal_ = true;
  this->sendGoal(goal,
                 //ClientBase::SimpleDoneCallback(),
                 boost::bind(&TrajectoryClient::doneCb, this, _1, _2),
                 //ClientBase::SimpleActiveCallback(),
                 boost::bind(&TrajectoryClient::activeCb, this),
                 //ClientBase::SimpleFeedbackCallback()
                 boost::bind(&TrajectoryClient::feedbackCb, this, _1)
                 );
}

void TrajectoryClient::send_angle_vector_sequence(const angle_vector_sequence &_av_seq, const time_vector &_tm_seq, const ros::Time &_start)
{
  if (_av_seq.size() != _tm_seq.size()) {
    ROS_ERROR("angle_vector_sequence: angle_vector_sequence size %ld != time_sequence size %ld",
              _av_seq.size(), _tm_seq.size()); //TODO
    return;
  }
  control_msgs::FollowJointTrajectoryGoal goal;
  goal.trajectory.header.stamp = _start;
  goal.trajectory.joint_names = joint_list_;
  goal.trajectory.points.resize(_av_seq.size());
  int jsize = joint_list_.size();
  double duration = 0;
  for(int i = 0; i < _av_seq.size(); i++) {
    if(jsize != _av_seq[i].size()) {
      ROS_ERROR("joint size %d != angle_vector size %ld", jsize, _av_seq[i].size());
      return;
    }
    goal.trajectory.points[i].positions.resize(jsize);
    for(int j = 0; j < jsize; j++) {
      goal.trajectory.points[i].positions[j] = _av_seq[i][j];
    }
    duration += _tm_seq[i];
    goal.trajectory.points[i].time_from_start = ros::Duration(duration);
  }
  goal.path_tolerance.resize(0);
  goal.goal_tolerance.resize(0);
  goal.goal_time_tolerance = ros::Duration(goal_time_tolerance_);
  //this->sendGoal(goal);
  sending_goal_ = true;
  this->sendGoal(goal,
                 //ClientBase::SimpleDoneCallback(),
                 boost::bind(&TrajectoryClient::doneCb, this, _1, _2),
                 //ClientBase::SimpleActiveCallback(),
                 boost::bind(&TrajectoryClient::activeCb, this),
                 //ClientBase::SimpleFeedbackCallback()
                 boost::bind(&TrajectoryClient::feedbackCb, this, _1)
                 );
}

bool TrajectoryClient::interpolatingp()
{
  actionlib::SimpleClientGoalState state = this->getState();
  ROS_DEBUG("interpolatingp %s", state.toString().c_str());
  return (state == actionlib::SimpleClientGoalState::StateEnum::ACTIVE);
}

void TrajectoryClient::stop_motion(double _stop_time)
{
  joint_angle_map av;
  getReferencePositions(av);

  sendAngles(av, _stop_time, ros::Time(0));
}

void TrajectoryClient::cancel_angle_vector (bool _wait)
{
  this->cancelAllGoals();
  if (_wait) {
    wait_interpolation(); // ??
  }
}

//// callback
void TrajectoryClient::StateCallback_(const control_msgs::JointTrajectoryControllerState::ConstPtr & _msg)
{
  // joint_states_ = *_msg;
  boost::mutex::scoped_lock lock(state_mtx_);
  current_state_ = *_msg;
  updated_state_ = true;
}

//// RobotInterface ////

RobotInterface::RobotInterface(ros::NodeHandle &_nh) : local_nh_(_nh), updated_joint_state_(false)
{
  joint_list_.resize(0);
  if (true) {
    joint_states_spinner_.reset(new ros::AsyncSpinner(1, &joint_states_queue_));
    ros::SubscribeOptions sub_ops = ros::SubscribeOptions::create< sensor_msgs::JointState >
      ( "joint_states", 10,
        boost::bind(&RobotInterface::JointStateCallback_, this, _1),
        ros::VoidPtr(), &joint_states_queue_);
    joint_states_sub_ = _nh.subscribe(sub_ops);
    joint_states_spinner_->start();
  } else {
    joint_states_sub_ = _nh.subscribe
      ("joint_states", 10, &RobotInterface::JointStateCallback_, this);
  }
  // wait first state !!!
  while(!updated_joint_state_) {
    ros::Duration d(0.1);
    d.sleep();
  }
}

RobotInterface::~RobotInterface()
{
  joint_states_spinner_->stop();
  {
    boost::mutex::scoped_lock lock(states_mtx_);
    joint_states_sub_.shutdown();
  }
  //ROS_WARN("~ RobotInterface ptr: %lX", (void *)this);
}

bool RobotInterface::defineJointList(std::vector < std::string > &_jl)
{
  boost::mutex::scoped_lock lock(states_mtx_);
  // joint existing check
  for(std::string jname: _jl) {
    bool found = false;
    for(auto it = controllers_.begin(); it != controllers_.end(); it++) {
      const std::vector <std::string > &ln = (it->second)->getJointNames();
      if (std::find(ln.begin(), ln.end(), jname) != ln.end()) {
        found = true;
        break;
      }
    }
    if(!found) {
      ROS_ERROR("joint name %s is not found", jname.c_str());
      return false;
    }
  }
  joint_list_ = _jl;
  return true;
}

bool RobotInterface::updateJointList()
{
  boost::mutex::scoped_lock lock(states_mtx_);
  for(auto it = controllers_.begin(); it != controllers_.end(); it++) {
    const std::vector< std::string > &names = (it->second)->getJointNames();
    std::copy( names.begin(), names.end(), std::back_inserter(joint_list_) );
  }
  return true;
}

bool RobotInterface::updateJointList(std::vector < std::string > &_controller_names_list)
{
  boost::mutex::scoped_lock lock(states_mtx_);
  for(std::string nm : _controller_names_list) {
    auto it = controllers_.find(nm);
    if (it != controllers_.end()) {
      const std::vector< std::string > &names = (it->second)->getJointNames();
      std::copy( names.begin(), names.end(), std::back_inserter(joint_list_) );
    } else {
      ROS_ERROR("can not find controller named %s", nm.c_str());
      return false;
    }
  }
  return true;
}

void RobotInterface::getReferencePositions( joint_angle_map &_map)
{
  boost::mutex::scoped_lock lock(states_mtx_);
  for(auto it = controllers_.begin(); it != controllers_.end(); it++) {
    joint_angle_map map;
    it->second->getReferencePositions(map);
    _map.insert(map.begin(), map.end());
  }
}

void RobotInterface::getActualPositions( joint_angle_map &_map)
{
  boost::mutex::scoped_lock lock(states_mtx_);
  _map = current_positions_;
}

bool RobotInterface::sendAngles(const joint_angle_map &_jmap,
                                const double _tm, const ros::Time &_start)
{
  for(auto it = controllers_.begin(); it != controllers_.end(); it++) {
    it->second->sendAngles(_jmap, _tm, _start);
  }
}

bool RobotInterface::sendAngles(const std::vector < std::string> &_names,
                                const std::vector< double> &_positions,
                                const double _tm, const ros::Time &_start)
{
  for(auto it = controllers_.begin(); it != controllers_.end(); it++) {
    it->second->sendAngles(_names, _positions, _tm, _start);
  }
}

void RobotInterface::send_angle_vector(const angle_vector &_av, const double _tm, const std::string &_name)
{
  std::vector< std::string> names;
  group2names_(_name, names);
  if(names.size() == 0) {
    std::vector< std::string> nms;
    nms.push_back(_name);
    send_angle_vector(_av, _tm, nms);
  } else {
    send_angle_vector(_av, _tm, names);
  }
}

void RobotInterface::send_angle_vector(const angle_vector &_av, const double _tm,
                                       const std::vector< std::string> &_names)
{
  for(auto it = controllers_.begin(); it != controllers_.end(); it++) {
    auto fname = std::find(_names.begin(), _names.end(), it->second->getName());
    if (fname != _names.end()) {
      angle_vector controller_av;
      if(it->second->convertToAngleVector(joint_list_, _av, controller_av)) {
        it->second->send_angle_vector(controller_av, _tm);
      }
    }
  }
}

void RobotInterface::send_angle_vector(const angle_vector &_av, const double _tm, const ros::Time &_start)
{
  for(auto it = controllers_.begin(); it != controllers_.end(); it++) {
    angle_vector controller_av;
    if(it->second->convertToAngleVector(joint_list_, _av, controller_av)) {
      it->second->send_angle_vector(controller_av, _tm, _start);
    }
  }
}

void RobotInterface::send_angle_vector_sequence(const angle_vector_sequence &_av_seq, const time_vector &_tm_seq,
                                                const std::string &_name, const ros::Time &_start)
{
  std::vector< std::string> names;
  group2names_(_name, names);
  if(names.size() == 0) {
    std::vector< std::string> nms;
    nms.push_back(_name);
    send_angle_vector_sequence(_av_seq, _tm_seq, nms, _start);
  } else {
    send_angle_vector_sequence(_av_seq, _tm_seq, names, _start);
  }
}

void RobotInterface::send_angle_vector_sequence(const angle_vector_sequence &_av_seq, const time_vector &_tm_seq,
                                                const std::vector< std::string> &_names, const ros::Time &_start)
{
  for(auto it = controllers_.begin(); it != controllers_.end(); it++) {
    auto fname = std::find(_names.begin(), _names.end(), it->second->getName());
    if (fname != _names.end()) {
      angle_vector_sequence controller_av_seq(0);
      for(angle_vector _av: _av_seq) {
        angle_vector controller_av;
        if(it->second->convertToAngleVector(joint_list_, _av, controller_av)) {
          controller_av_seq.push_back(controller_av);
        }
      }
      //ROS_INFO("av %ld (%ld), tm %ld", controller_av_seq.size(),
      //controller_av_seq[0].size(), _tm_seq.size());
      it->second->send_angle_vector_sequence(controller_av_seq, _tm_seq, _start);
    }
  }
}

void RobotInterface::send_angle_vector_sequence(const angle_vector_sequence &_av_seq, const time_vector &_tm_seq, const ros::Time &_start)
{
  for(auto it = controllers_.begin(); it != controllers_.end(); it++) {
    angle_vector_sequence controller_av_seq(0);
    for(angle_vector _av: _av_seq) {
      angle_vector controller_av;
      if(it->second->convertToAngleVector(joint_list_, _av, controller_av)) {
        controller_av_seq.push_back(controller_av);
      }
    }
    it->second->send_angle_vector_sequence(controller_av_seq, _tm_seq, _start);
  }
}

bool RobotInterface::add_controller (const std::string &_key,
                                     const std::string &_action_name,
                                     const std::string &_state_name,
                                     const std::vector< std::string> &_jnames,
                                     bool _update_joint_list)
{
  boost::shared_ptr<TrajectoryClient > p;
  {
    boost::mutex::scoped_lock lock(states_mtx_);
    p.reset(new TrajectoryClient(local_nh_, _action_name, _state_name, _jnames));

    if (!p->isServerConnected()) {
      return false;
    }
  }
  return this->add_controller(_key, p, _update_joint_list);
}

bool RobotInterface::add_controller(const std::string &_key,
                                    const TrajectoryClient::Ptr &_p,
                                    bool _update_joint_list)
{
  boost::mutex::scoped_lock lock(states_mtx_);
  if (controllers_.find(_key) == controllers_.end()) {
    controllers_[_key] = _p;
    if (_update_joint_list) {
      const std::vector< std::string > &names = _p->getJointNames();
      std::copy( names.begin(), names.end(), std::back_inserter(joint_list_) );
    }
    _p->setName(_key);
    return true;
  }
  ROS_ERROR("the same name %s controller already esists", _key.c_str());
  return false;
}

bool RobotInterface::wait_interpolation(double _tm)
{
  bool ret = true;
  bool with_limit = (_tm != 0.0);
  ros::Time tm_limit = ros::Time::now() + ros::Duration(_tm);
  double remain_tm = _tm;

  for(auto it = controllers_.begin(); it != controllers_.end(); it++) {
    ROS_DEBUG("wait (%s), state = %s ", (it->first).c_str(),
              (it->second)->getState().toString().c_str());
    if( !((it->second)->wait_interpolation(remain_tm)) ) {
      ret = false;
      break;
    }
    if (with_limit) {
      double diff = (tm_limit - ros::Time::now()).toSec();
      if (diff  <= 0.0 ) {
        remain_tm = 0.000001; // very short time
      } else {
        remain_tm = diff;
      }
    }
  }

  return ret;
}

bool RobotInterface::wait_interpolation(const std::string &_name, double _tm)
{
  std::vector<std::string > names;
  group2names_(_name, names);
  if(names.size() == 0) {
    names.push_back(_name);
  }
  return wait_interpolation(names, _tm);
}

bool RobotInterface::wait_interpolation(const std::vector<std::string > &_names, double _tm)
{
  bool ret = true;
  bool with_limit = (_tm != 0.0);
  ros::Time tm_limit = ros::Time::now() + ros::Duration(_tm);
  double remain_tm = _tm;

  for(auto it = _names.begin(); it != _names.end(); it++) {
    auto cit = controllers_.find(*it);
    if(cit != controllers_.end()) {
      if( !((cit->second)->wait_interpolation(remain_tm)) ) {
        ret = false;
        break;
      }
    }
    if (with_limit) {
      double diff = (tm_limit - ros::Time::now()).toSec();
      if (diff  <= 0.0 ) {
        remain_tm = 0.000001; // very short time
      } else {
        remain_tm = diff;
      }
    }
  }
  return ret;
}

bool RobotInterface::interpolatingp ()
{
  bool ret = false;
  for(auto it = controllers_.begin(); it != controllers_.end(); it++) {
    if ( (it->second)->interpolatingp() ) {
      ret = true;
      break;
    }
  }
  if (!ret) {
    return !wait_interpolation(0.00001);
  }
  return ret;
}
bool RobotInterface::interpolatingp (const std::string &_name)
{
  std::vector<std::string > names;
  group2names_(_name, names);
  if(names.size() == 0) {
    names.push_back(_name);
  }
  return interpolatingp(names);
}
bool RobotInterface::interpolatingp (const std::vector<std::string > &_names)
{
  bool ret = false;
  for(auto it = _names.begin(); it != _names.end(); it++) {
    auto cit = controllers_.find(*it);
    if(cit != controllers_.end()) {
      if ( (cit->second)->interpolatingp() ) {
        ret = true;
        break;
      }
    }
  }
  if (!ret) {
    return !wait_interpolation(_names, 0.00001);
  }
  return ret;
}

void RobotInterface::stop_motion(double _stop_time)
{
  for(auto it = controllers_.begin(); it != controllers_.end(); it++) {
    (it->second)->stop_motion(_stop_time);
  }
}
void RobotInterface::stop_motion(const std::string &_name, double _stop_time)
{
  std::vector<std::string > names;
  group2names_(_name, names);
  if(names.size() == 0) {
    names.push_back(_name);
  }
  stop_motion(names, _stop_time);
}
void RobotInterface::stop_motion(const std::vector<std::string > &_names, double _stop_time)
{
  for(auto it = _names.begin(); it != _names.end(); it++) {
    auto cit = controllers_.find(*it);
    if( cit != controllers_.end() ) {
      (cit->second)->stop_motion(_stop_time);
    }
  }
}

void RobotInterface::cancel_angle_vector (bool _wait)
{
  for(auto it = controllers_.begin(); it != controllers_.end(); it++) {
    (it->second)->cancel_angle_vector(false);
  }
  if (_wait) {
    wait_interpolation();
  }
}
void RobotInterface::cancel_angle_vector (const std::string &_name, bool _wait)
{
  std::vector<std::string > names;
  group2names_(_name, names);
  if(names.size() == 0) {
    names.push_back(_name);
  }
  cancel_angle_vector(names, _wait);
}
void RobotInterface::cancel_angle_vector (const std::vector<std::string > &_names, bool _wait)
{
  for(auto it = _names.begin(); it != _names.end(); it++) {
    auto cit = controllers_.find(*it);
    if( cit != controllers_.end() ) {
      (cit->second)->cancel_angle_vector(_wait);
    }
  }
  if (_wait) {
    wait_interpolation();
  }
}

bool RobotInterface::add_group(const std::string &_name, const std::vector<std::string > &_names)
{
  // TODO: check existance of _names
  controller_group_[_name] = _names;
  return true;
}

bool RobotInterface::configureFromParam(const std::string &_param)
{
  if (local_nh_.hasParam(_param)) {
    std::vector<std::string > lst;
    local_nh_.getParam(_param, lst);
    for(int i = 0; i < lst.size(); i++) {
      ROS_DEBUG("controller: %s", lst[i].c_str());
      std::string p = lst[i] + "_controller/joints";
      if(local_nh_.hasParam(p)) {
        std::vector<std::string > jt;
        local_nh_.getParam(p, jt);
        for(int j = 0; j < jt.size(); j++) {
          ROS_DEBUG("  j_%d: %s", j, jt[j].c_str());
        }
        if (jt.size() > 0) {
          add_controller(lst[i],
                         lst[i] + "_controller/follow_joint_trajectory",
                         lst[i] + "_controller/state",
                         jt);
        }
      }
    }
  } else {
    ROS_WARN("there is no param: %s%s",
             local_nh_.getNamespace().c_str(), _param.c_str());
  }
}

//// callback
void RobotInterface::JointStateCallback_(const sensor_msgs::JointState::ConstPtr& _msg)
{
  // joint_states_ = *_msg;
  boost::mutex::scoped_lock lock(states_mtx_);

  current_stamp_ = _msg->header.stamp;
  for(int i = 0; i < _msg->position.size(); i++) {
    current_positions_[_msg->name[i]] = _msg->position[i];
  }
  for(int i = 0; i < _msg->velocity.size(); i++) {
    current_velocities_[_msg->name[i]] = _msg->velocity[i];
  }
  for(int i = 0; i < _msg->effort.size(); i++) {
    current_effort_[_msg->name[i]] = _msg->effort[i];
  }
  updated_joint_state_ = true;
}
