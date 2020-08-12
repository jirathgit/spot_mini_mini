/// \file
/// \brief Teensy Main.

#include <Arduino.h>
#include <SpotServo.hpp>
#include <ContactSensor.hpp>
#include <Kinematics.hpp>
#include <Utilities.hpp>
#include <IMU.hpp>
#include <Servo.h>
#include <ROSSerial.hpp>

#define DEBUGSERIAL Serial

bool ESTOPPED = false;
int viewing_speed = 400; // doesn't really mean anything, theoretically deg/sec
int walking_speed = 1500; // doesn't really mean anything, theoretically deg/sec
double last_estop = millis();
static unsigned long prev_publish_time;
const int ledPin = 13;


SpotServo FL_Shoulder, FL_Elbow, FL_Wrist, FR_Shoulder, FR_Elbow, FR_Wrist, BL_Shoulder, BL_Elbow, BL_Wrist, BR_Shoulder, BR_Elbow, BR_Wrist;
ContactSensor FL_sensor, FR_sensor, BL_sensor, BR_sensor;
SpotServo * Shoulders[4] = {&FL_Shoulder, &FR_Shoulder, &BL_Shoulder, &BR_Shoulder};
SpotServo * Elbows[4] = {&FL_Elbow, &FR_Elbow, &BL_Elbow, &BR_Elbow};
SpotServo * Wrists[4] = {&FL_Wrist, &FR_Wrist, &BL_Wrist, &BR_Wrist};
SpotServo * AllServos[12] = {&FL_Shoulder, &FL_Elbow, &FL_Wrist, &FR_Shoulder, &FR_Elbow, &FR_Wrist, &BL_Shoulder, &BL_Elbow, &BL_Wrist, &BR_Shoulder, &BR_Elbow, &BR_Wrist};
Utilities util;
Kinematics ik;
IMU imu_sensor;
LegType fl_leg = FL;
LegType fr_leg = FR;
LegType bl_leg = BL;
LegType br_leg = BR;
LegJoints FL_ = LegJoints(fl_leg);
LegJoints FR_ = LegJoints(fr_leg);
LegJoints BL_ = LegJoints(bl_leg);
LegJoints BR_ = LegJoints(br_leg);
ros_srl::ROSSerial ros_serial;

void update_servos()
{
    // ShoulderS
    FL_Shoulder.update_clk();
    FR_Shoulder.update_clk();
    BL_Shoulder.update_clk();
    BR_Shoulder.update_clk();

    // Elbow
    FL_Elbow.update_clk();
    FR_Elbow.update_clk();
    BL_Elbow.update_clk();
    BR_Elbow.update_clk();

    // WRIST
    FL_Wrist.update_clk();
    FR_Wrist.update_clk();
    BL_Wrist.update_clk();
    BR_Wrist.update_clk();
}

void command_servos(const LegJoints & legjoint, const bool & step_or_view = true)
{
  int leg = -1;

  if (legjoint.legtype == FL)
  {
    leg = 0;
  } else if (legjoint.legtype == FR)
  {
    leg = 1;
  } else if (legjoint.legtype == BL)
  {
    leg = 2;
  } else if (legjoint.legtype == BR)
  {
    leg = 3;
  }


  double shoulder_home = (*Shoulders[leg]).return_home();
  double elbow_home = (*Elbows[leg]).return_home();
  double wrist_home = (*Wrists[leg]).return_home();

  double Shoulder_angle = util.angleConversion(legjoint.shoulder, shoulder_home, legjoint.legtype, Shoulder);
  double Elbow_angle = util.angleConversion(legjoint.elbow, elbow_home, legjoint.legtype, Elbow);
  double Wrist_angle = util.angleConversion(legjoint.wrist, wrist_home, legjoint.legtype, Wrist);

  double s_dist = abs(Shoulder_angle - (*Shoulders[leg]).GetPoseEstimate());
  double e_dist = abs(Elbow_angle - (*Elbows[leg]).GetPoseEstimate());
  double w_dist = abs(Wrist_angle - (*Wrists[leg]).GetPoseEstimate());

  double scaling_factor = util.max(s_dist, e_dist, w_dist);

  s_dist /= scaling_factor;
  e_dist /= scaling_factor;
  w_dist /= scaling_factor;

  double s_speed = 0.0;
  double e_speed = 0.0;
  double w_speed = 0.0;

  if (step_or_view)
  {
    s_speed = viewing_speed * s_dist;
    s_speed = max(s_speed, viewing_speed / 10.0);
    e_speed = viewing_speed * e_dist;
    e_speed = max(e_speed, viewing_speed / 10.0);
    w_speed = viewing_speed * w_dist;
    w_speed = max(w_speed, viewing_speed / 10.0);
  } else
  {
    s_speed = walking_speed * s_dist;
    e_speed = walking_speed * e_dist;
    w_speed = walking_speed * w_dist;
  }

  (*Shoulders[leg]).SetGoal(Shoulder_angle, s_speed, step_or_view);
  (*Elbows[leg]).SetGoal(Elbow_angle, e_speed, step_or_view);
  (*Wrists[leg]).SetGoal(Wrist_angle, w_speed, step_or_view);
  
}

void update_sensors()
{
    FL_sensor.update_clk();
    FR_sensor.update_clk();
    BL_sensor.update_clk();
    BR_sensor.update_clk();
}

void set_stance(const double & f_shoulder_stance = 0.0, const double & f_elbow_stance = 0.0, const double & f_wrist_stance = 0.0,
                const double & r_shoulder_stance = 0.0, const double & r_elbow_stance = 0.0, const double & r_wrist_stance = 0.0)
{
  // Legs
  FL_.FillLegJoint(f_shoulder_stance, f_elbow_stance, f_wrist_stance);
  FR_.FillLegJoint(f_shoulder_stance, f_elbow_stance, f_wrist_stance);
  BL_.FillLegJoint(r_shoulder_stance, r_elbow_stance, r_wrist_stance);
  BR_.FillLegJoint(r_shoulder_stance, r_elbow_stance, r_wrist_stance);

  command_servos(FL_);
  command_servos(FR_);
  command_servos(BL_);
  command_servos(BR_);

  // Loop until goal reached - check BR Wrist (last one)
  while (!BR_Wrist.GoalReached())
  {
    update_servos();
  }
}

void run_sequence()
{
  // Move to Crouching Stance
  delay(2000);
  double f_shoulder_stance = 0.0;
  double f_elbow_stance =  36.13;
  double f_wrist_stance = -75.84;
  double r_shoulder_stance = 0.0;
  double r_elbow_stance =  45.41;
  double r_wrist_stance = -75.84;
  set_stance(f_shoulder_stance, f_elbow_stance, f_wrist_stance, r_shoulder_stance, r_elbow_stance, r_wrist_stance);

  // Contact Sensors
  FL_sensor.Initialize(A9, 17);
  FR_sensor.Initialize(A8, 16);
  BL_sensor.Initialize(A7, 15);
  BR_sensor.Initialize(A6, 14);

  // IMU
  imu_sensor.Initialize();

  // Move to Extended stance
  delay(100);
  set_stance();

  // Indicate end of init
  // Move to Crouching Stance
  delay(500);
  set_stance(f_shoulder_stance, f_elbow_stance, f_wrist_stance, r_shoulder_stance, r_elbow_stance, r_wrist_stance);
}

void straight_calibration_sequence()
{

  // Move to Extended stance
  // Contact Sensors
  FL_sensor.Initialize(A9, 17);
  FR_sensor.Initialize(A8, 16);
  BL_sensor.Initialize(A7, 15);
  BR_sensor.Initialize(A6, 14);

  // IMU
  imu_sensor.Initialize();

  set_stance();

}

void lie_calibration_sequence()
{

  // Move to Extended stance
  delay(2000);
  double shoulder_stance = 0.0;
  double elbow_stance =  90.0;
  double wrist_stance = -170.3;
  set_stance(shoulder_stance, elbow_stance, wrist_stance, shoulder_stance, elbow_stance, wrist_stance);

  // Contact Sensors
  FL_sensor.Initialize(A9, 17);
  FR_sensor.Initialize(A8, 16);
  BL_sensor.Initialize(A7, 15);
  BR_sensor.Initialize(A6, 14);

  // IMU
  imu_sensor.Initialize();

}

void perpendicular_calibration_sequence()
{

  // Move to Extended stance
  delay(10000);
  set_stance();
  // Move to Extended stance
  delay(10000);
  double shoulder_stance = 0.0;
  double elbow_stance =  90.0;
  double wrist_stance = -90.0;
  set_stance(shoulder_stance, elbow_stance, wrist_stance, shoulder_stance, elbow_stance, wrist_stance);

}

// THIS ONLY RUNS ONCE
void setup() {

  Serial.begin(9600);

  // IK - unused
  ik.Initialize(0.04, 0.1, 0.1);

  // SERVOS: Pin, StandAngle, HomeAngle, Offset, LegType, JointType
  // Shoulders
  double shoulder_liedown = 0.0;
  FL_Shoulder.Initialize(2, 135 + shoulder_liedown, 135, -10.0, FL, Shoulder, 500, 2500);  // 0 | 135 mid - 0 out - 270 in
  FR_Shoulder.Initialize(5, 135 - shoulder_liedown, 135, -6.0, FR, Shoulder, 500, 2500); // 1 | 135 mid - 270 out - 0 in
  BL_Shoulder.Initialize(8, 135 + shoulder_liedown, 135, 0.0, BL, Shoulder, 500, 2500);  // 2 | 135 mid - 0 out - 270 in
  BR_Shoulder.Initialize(11, 135 - shoulder_liedown, 135, -6.0, BR, Shoulder, 500, 2500);  // 3 | 135 mid - 270 out - 0 in
  
  //Elbows
  double elbow_liedown = 90.0;
  FL_Elbow.Initialize(3, 135 + elbow_liedown, 135, -14.5, FL, Elbow, 500, 2500);  // 4 | 135  mid - 0 in front - 270 behind
  FR_Elbow.Initialize(6, 135 - elbow_liedown, 135, -16.5, FR, Elbow, 500, 2500); // 5 | 135  mid - 0 in behind - 270 in front
  BL_Elbow.Initialize(9, 135 + elbow_liedown, 135, -10.0, BL, Elbow, 500, 2500);  // 6 | 135  mid - 0 in front - 270 behind
  BR_Elbow.Initialize(12, 135 - elbow_liedown, 135, -0.25, BR, Elbow, 500, 2500); // 7 | 135  mid - 0 in behind - 270 in front

  //Wrists
  double wrist_liedown = 156.8;
  FL_Wrist.Initialize(4, 90 + wrist_liedown, 90, 0.0, FL, Wrist, 500, 2500);  // 8 | 90 straight - 270 bent in
  FR_Wrist.Initialize(7, 180 - wrist_liedown, 180, 0.0, FR, Wrist, 500, 2500); // 9 | 180 straight - 0 bent in
  BL_Wrist.Initialize(10, 90 + wrist_liedown, 90, 0.0, BL, Wrist, 500, 2500); // 10 | 90 straight - 270 bent in
  BR_Wrist.Initialize(13, 180 - wrist_liedown, 180, 0.0, BR, Wrist, 500, 2500); // 11 | 180 straight - 0 bent in

  // pick one
  // run_sequence();
  straight_calibration_sequence();
  // lie_calibration_sequence();

  last_estop = millis();

  prev_publish_time = micros();
}

// THIS LOOPS FOREVER
void loop()
{
  // CHECK SENSORS AND SEND INFO
  // CONTACT
  // 1'000'000 / 20'000 = 50hz
  if ((micros() - prev_publish_time) >= 20000)
  {
    ros_serial.publishContacts(FL_sensor.isTriggered(), FR_sensor.isTriggered(), BL_sensor.isTriggered(), BR_sensor.isTriggered());

    //IMU
    if (imu_sensor.available())
    {
      imu::Vector<3> eul = imu_sensor.GetEuler();
      imu::Vector<3> acc = imu_sensor.GetAcc();
      imu::Vector<3> gyro = imu_sensor.GetGyro();

      ros_serial.publishIMU(eul, acc, gyro);
    }

    prev_publish_time = micros();
  }

  if(!ESTOPPED){
    update_servos();
  }
  update_sensors();
  
  // Command Servos
  if (ros_serial.jointsInputIsActive())
  {
    bool step_or_view = false;
    ros_serial.returnJoints(FL_, FR_, BL_, BR_, step_or_view);
    command_servos(FL_, step_or_view);
    command_servos(FR_, step_or_view);
    command_servos(BL_, step_or_view);
    command_servos(BR_, step_or_view);
  } else if (ros_serial.jointsPulseIsActive())
  {
    int servo_num = ros_serial.returnServoNum();
    int pulse = ros_serial.returnPulse();

    if (servo_num != -1)
    {
      AllServos[servo_num]->writePulse(pulse);
    }

    ros_serial.resetPulseTopic();
  }

  // Update ROS Node (spinOnce etc...)
  ros_serial.run();
}
