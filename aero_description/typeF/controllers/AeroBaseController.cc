#include "aero_move_base/AeroMoveBase.hh"

using namespace aero;
using namespace navigation;

static const float radius = 0.076;
static const float Radius = 0.2974535;
// static const float max_velocity = 450.0;  // rpm * 10
static const float max_velocity = 90.0;  // deg/s

static const float ktheta = -5.54420;
static const float kv = 13.1579;

//////////////////////////////////////////////////
class AeroMoveBase::AeroMoveBaseImpl
{
public: AeroMoveBaseImpl();

public: ~AeroMoveBaseImpl();

public: wheels Translate(float _x, float _y);

public: wheels Rotate(float _theta);

public: wheels Drift(float _x, float _theta);
};

//////////////////////////////////////////////////
void AeroMoveBase::Init()
{
  impl_.reset(new AeroMoveBase::AeroMoveBaseImpl());

  ros_rate_ = 0.05;
  odom_rate_ = 0.02;
  safe_rate_ = 0.5;
  safe_duration_ = 1.0;
  num_of_wheels_ = 4;
  wheel_names_ =
    {"can_front_l_wheel", "can_front_r_wheel",
     "can_rear_l_wheel", "can_rear_r_wheel"};
}

//////////////////////////////////////////////////
wheels AeroMoveBase::Translate(float _x, float _y, float _theta)
{
  wheels wheel_data;

  if (fabs(_theta) < 0.0001) // if translate
  {
    wheel_data = this->impl_->Translate(_x , _y);
  }
  else // if has rotate
  {
    if (fabs(_x) < 0.0001) // only rotate
      wheel_data = this->impl_->Rotate(_theta);
    else
      wheel_data = this->impl_->Drift(_x, _theta);
  }

  return wheel_data;
}

//////////////////////////////////////////////////
pose AeroMoveBase::dX(std::vector<double> _vels, float _dt)
{
  if (_vels.size() != 4) return {0.0, 0.0, 0.0};

  std::vector<double> velocities = // signal positive to move forward positive
      {-_vels[0], _vels[1], -_vels[2], _vels[3]};

  float Vx =
      0.25 * 0.5 *
      static_cast<float>(velocities[0] + velocities[1] + velocities[2] + velocities[3]);
  float Vy =
      0.25 * 0.5 *
      static_cast<float>(-velocities[0] + velocities[1] + velocities[2] - velocities[3]);

  return {static_cast<float>(Vx * 2*M_PI * radius * _dt),
      static_cast<float>(Vy * 2*M_PI * radius * _dt),
      static_cast<float>(max_velocity * radius * M_PI * _dt / (sqrt(2) * Radius * 300))};
}

//////////////////////////////////////////////////
void AeroMoveBase::VelocityToWheel(
    const geometry_msgs::TwistConstPtr& _cmd_vel,
    std::vector<double>& _wheel_vel)
{
  float dx, dy, dtheta, theta;
  float v1, v2, v3, v4;
  int16_t FR_wheel, RR_wheel, FL_wheel, RL_wheel;
  theta = 0.0;  // this means angle in local coords, so always 0

  float cos_theta = cos(theta);
  float sin_theta = sin(theta);

  // change dy and dx, because of between ROS and vehicle direction
  dy = (_cmd_vel->linear.x * cos_theta - _cmd_vel->linear.y * sin_theta);
  dx = (_cmd_vel->linear.x * sin_theta + _cmd_vel->linear.y * cos_theta);
  dtheta = _cmd_vel->angular.z;  // desirede angular velocity

  // calculate wheel velocity
  v1 = ktheta * dtheta +
      kv * ((-cos_theta + sin_theta) * dx + (-cos_theta - sin_theta) * dy);
  v2 = ktheta * dtheta +
      kv * ((-cos_theta - sin_theta) * dx + ( cos_theta - sin_theta) * dy);
  v3 = ktheta * dtheta +
      kv * (( cos_theta - sin_theta) * dx + ( cos_theta + sin_theta) * dy);
  v4 = ktheta * dtheta +
      kv * (( cos_theta + sin_theta) * dx + (-cos_theta + sin_theta) * dy);

  //[rad/sec] -> [deg/sec]
  FR_wheel = static_cast<int16_t>(v1 * (180 / M_PI));
  RR_wheel = static_cast<int16_t>(v4 * (180 / M_PI));
  FL_wheel = static_cast<int16_t>(v2 * (180 / M_PI));
  RL_wheel = static_cast<int16_t>(v3 * (180 / M_PI));

  _wheel_vel[0] = FL_wheel;
  _wheel_vel[1] = FR_wheel;
  _wheel_vel[2] = RL_wheel;
  _wheel_vel[3] = RR_wheel;
}

//////////////////////////////////////////////////
AeroMoveBase::AeroMoveBaseImpl::AeroMoveBaseImpl()
{
}

//////////////////////////////////////////////////
AeroMoveBase::AeroMoveBaseImpl::~AeroMoveBaseImpl()
{
}

//////////////////////////////////////////////////
wheels AeroMoveBase::AeroMoveBaseImpl::Translate(float _x, float _y)
{
  // front_left and rear_right
  std::function<float(float, float)> lambda_vel1 =
      [=](float x, float y)
      {
        float result;

        if (x > 0 && y > 0)
        {
          float theta = atan(y / x);
          result = max_velocity * 4/M_PI * (0.25*M_PI - theta);
        }
        else if (y >= 0 && x <= 0)
          result = -max_velocity;
        else if (y <= 0 && x >= 0)
          result = max_velocity;
        else
        {
          float theta = atan(y / x) - M_PI;
          result = max_velocity * 4/M_PI * (theta + 0.75*M_PI);
        }

        return result;
      };

  // front_right and rear_left
  std::function<float(float, float)> lambda_vel2 =
      [=](float x, float y)
      {
        float result;

        if (x >= 0 && y >= 0)
          result = max_velocity;
        else if (y > 0 && x < 0)
        {
          float theta = M_PI - atan(y / x);
          result = max_velocity * 4/M_PI * (0.75*M_PI - theta);
        }
        else if (y < 0 && x > 0)
        {
          float theta = atan(y / x);
          result = max_velocity * 4/M_PI * (theta + 0.25*M_PI);
        }
        else
          result = -max_velocity;

        return result;
      };

  std::vector<float> velocities =
      {lambda_vel1(_x, _y), lambda_vel2(_x, _y),
       lambda_vel2(_x, _y), lambda_vel1(_x ,_y)};

  // the wheel velocities are velocities[i]
  // the omni wheel direction is in the 45[deg] direction of the wheel
  // therefore, the actual speed of the robot is velocities[i]/sqrt(2)
  // decomposing the actual speed to the X and Y direction results to
  // velocities[i]/sqrt(2) * 1/sqrt(2) = 0.5 * velocities[i]
  // there is four wheels so the velocities are averaged with 0.25
  float Vx =
    // 0.25 * 0.5 *
    0.25 * sqrt(2.0) * 0.5 *
      (velocities[0] + velocities[1] + velocities[2] + velocities[3]);
  float Vy =
    // 0.25 * 0.5 *
    0.25 * sqrt(2.0) * 0.5 *
      (-velocities[0] + velocities[1] + velocities[2] - velocities[3]);

  float distance = sqrt(_x * _x + _y * _y);  // m
  float velocity_radian =
    sqrt(Vx * Vx + Vy * Vy) * M_PI / 180;  // deg/s -> rad/s
  float wheel_travel = velocity_radian * radius;  // m/s

  wheels wheel_data;
  wheel_data.velocities = // move forward positive to signal positive
    {velocities[0], -velocities[1], velocities[2], -velocities[3]};
  wheel_data.time =
    distance / wheel_travel / 1.4;
    // sqrt(_x*_x + _y*_y) / (sqrt(Vx*Vx + Vy*Vy) * M_PI * radius) * 300;
  // * 60 is rpm -> rps

  return wheel_data;
}

//////////////////////////////////////////////////
wheels AeroMoveBase::AeroMoveBaseImpl::Rotate(float _theta)
{
  wheels wheel_data;

  if (_theta >= 0)
    wheel_data.velocities =
        {-max_velocity, -max_velocity, -max_velocity, -max_velocity};
  else
    wheel_data.velocities =
        {max_velocity, max_velocity, max_velocity, max_velocity};

  float velocity_radian =
    max_velocity * M_PI / 180;  // deg/s -> rad/s
  float wheel_travel =
    radius * velocity_radian;  // rad/s -> m/s
  float turn_radius =
    Radius * fabs(_theta);  // rad -> m

  // wheel_data.time =
  //     300 * sqrt(2) * Radius * fabs(_theta) / (M_PI * radius * max_velocity);
  wheel_data.time = turn_radius / wheel_travel;

  return wheel_data;
}

//////////////////////////////////////////////////
wheels AeroMoveBase::AeroMoveBaseImpl::Drift(float _x, float _theta)
{
  // Note : drift rotates at half the speed of Rotate

  wheels wheel_data;

  float x = fabs(_x);
  float theta = fabs(_theta);
  // moving forward and turning more than 90 degrees is not possible
  if (theta > (M_PI * 0.5)) theta = M_PI * 0.5;

  // Time calculated from x movement is equal to
  // time calculated from theta movement.
  // Note, because of the rotation,
  // the x movement is the integral of V*cos(theta)dt.
  // Thus, time from x is expressed with sin(theta)/theta.
  // By using the two equations and eliminating v_theta,
  // time can be expressed as the following.
  float time = theta * 600 / (M_PI * radius * max_velocity) *
      (x / sin(theta) + sqrt(2) * Radius);
  if (time == 0)
  {
    ROS_ERROR("unexpected time = 0");
    return {{0.0, 0.0, 0.0, 0.0}, 0.0};
  }

  // When left or right side wheels move at speed V,
  // the other side moves at V-v_theta,
  // which means, the robot moves forward at V-v_theta
  // and the difference between the wheels create the rotation.
  // The rotation velocity is equal to v_theta/2, because
  // the sided difference is equavalent to
  // +v_theta/2 on one side, and -v_theta/2 on the other.
  float v_theta =
      2 * theta * sqrt(2) * Radius / (radius * time * M_PI) * 300;
  // The actual command to send is V-v_theta,
  // v_turn_wheel is backward positive
  float v_turn_wheel = v_theta - max_velocity;

  if (_theta >= 0)
  {
    if (_x >= 0)
      wheel_data.velocities =
          {-v_turn_wheel, -max_velocity,
           -v_turn_wheel, -max_velocity};
    else
      wheel_data.velocities =
          {-max_velocity, -v_turn_wheel,
           -max_velocity, -v_turn_wheel};
  }
  else
  {
    if (_x <= 0)
      wheel_data.velocities =
          {v_turn_wheel, max_velocity,
                v_turn_wheel, max_velocity};
    else
      wheel_data.velocities =
          {max_velocity, v_turn_wheel,
           max_velocity, v_turn_wheel};
  }
  wheel_data.time = time;

  return wheel_data;
}
