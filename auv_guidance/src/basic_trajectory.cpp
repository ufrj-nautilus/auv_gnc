#include "auv_guidance/basic_trajectory.hpp"

namespace auv_guidance
{
/**
 * @param constraints Pointer to AUV constraints
 * @param start Starting waypoint
 * @param end Ending waypoint
 */
BasicTrajectory::BasicTrajectory(auv_core::auvConstraints *constraints, Waypoint *wStart, Waypoint *wEnd)
{
   auvConstraints_ = constraints;
   wStart_ = wStart;
   wEnd_ = wEnd;

   qStop_.setIdentity();
   deltaVec_.setZero();
   unitVec_.setZero();
   maxVelocityVec_.setZero();

   maxVelocityST_ = 0;
   maxVelocity_ = 0;
   stopDuration_ = 0;
   simultaneousDuration_ = 0;
   longDuration_ = 0;
   distance_ = 0;

   isLongTrajectory_ = false;
   isSimultaneousTrajectory_ = true;
   exceedsMaxSpeed_ = false;

   BasicTrajectory::setStopTrajectory();
   BasicTrajectory::setPrimaryTrajectory();
}

/**
 * \brief Set the trajectory that brings vehicle to a stop
 */
void BasicTrajectory::setStopTrajectory()
{
   // Get stop position
   double transVel = wStart_->velI().norm();
   double transAccel = wStart_->accelI().norm();
   double accel = 0;
   if (fabs(wStart_->velI()(0)) > fabs(wStart_->velI()(1))) // Xvel > Yvel
   {
      accel = auvConstraints_->maxTransAccel(0);
      if (fabs(wStart_->velI()(2)) > fabs(wStart_->velI()(0))) // Zvel > Xvel
         accel = auvConstraints_->maxTransAccel(2);
   }
   else
   {
      accel = auvConstraints_->maxTransAccel(1);
      if (fabs(wStart_->velI()(2)) > fabs(wStart_->velI()(1))) // Zvel > Yvel
         accel = auvConstraints_->maxTransAccel(2);
   }
   double distance = (2.0 / 3.0) * (transVel * transVel) / accel;
   Eigen::Vector3d restDeltaVec = wStart_->velI().normalized() * distance;
   Eigen::Vector3d stopPos = wStart_->posI() + restDeltaVec;

   // Get stop quaternion
   Eigen::Vector4d angleAxis = Eigen::Vector4d::Zero();
   angleAxis.tail<3>() = wStart_->angVelB().normalized();
   double angVel = wStart_->angVelB().norm(); // Magnitude
   double angularDistance = (2.0 / 3.0) * (angVel * angVel) / auvConstraints_->maxRotAccel;
   angleAxis(0) = angularDistance;

   Eigen::Quaterniond qRotate = auv_core::rot3d::angleAxis2Quat(angleAxis); // Relative to B-frame
   qStop_ = wStart_->quaternion() * qRotate;                                // Apply qRotate FROM qStart

   Eigen::Vector3d zero3d = Eigen::Vector3d::Zero();
   wStop_ = new Waypoint(stopPos, zero3d, zero3d, qStop_, zero3d);

   // Find travel time for translation and rotation, take the longer one
   Eigen::Vector4d transStart = Eigen::Vector4d::Zero();
   Eigen::Vector4d transEnd = Eigen::Vector4d::Zero();
   Eigen::Vector4d rotStart = Eigen::Vector4d::Zero();
   Eigen::Vector4d rotEnd = Eigen::Vector4d::Zero();

   transStart << 0, transVel, transAccel, auvConstraints_->transJerk;
   transEnd << distance, 0, 0, auvConstraints_->transJerk;
   rotStart << 0, angVel, 0, auvConstraints_->rotJerk;
   rotEnd << angularDistance, 0, 0, auvConstraints_->rotJerk;

   double timeTrans = 0, timeRot = 0;
   MinJerkTimeSolver *mjts;
   mjts = new MinJerkTimeSolver(transStart, transEnd);
   timeTrans = mjts->getTime();
   mjts = new MinJerkTimeSolver(rotStart, rotEnd);
   timeRot = mjts->getTime();
   stopDuration_ = std::max(timeTrans, timeRot); // Take longer duration

   stStop_ = new SimultaneousTrajectory(wStart_, wStop_, stopDuration_);
   totalDuration_ = stopDuration_;
}

/**
 * \brief Determine the maximum velocity if using a simultaneous trajectory
 */
void BasicTrajectory::computeMaxVelocityST()
{
   deltaVec_ = wEnd_->posI() - wStop_->posI();
   unitVec_ = deltaVec_.normalized();
   distance_ = deltaVec_.norm();

   BasicTrajectory::computeSimultaneousTime();

   // Find max translational velocity
   Eigen::Vector3d transStart = Eigen::Vector3d::Zero();
   Eigen::Vector3d transEnd = Eigen::Vector3d::Zero();
   transEnd(0) = distance_;
   mjtHelper_ = new MinJerkTrajectory(transStart, transEnd, simultaneousDuration_);

   maxVelocityST_ = fabs(mjtHelper_->getMiddleVelocity());
   maxVelocity_ = maxVelocityST_;
   maxVelocityVec_ = unitVec_ * maxVelocity_;
}

/**
 * \brief Find the duration of a simulaneous trajectory from stop point to goal point
 */
void BasicTrajectory::computeSimultaneousTime()
{
   // Translation
   Eigen::Vector4d transStart = Eigen::Vector4d::Zero();
   Eigen::Vector4d transEnd = Eigen::Vector4d::Zero();
   transStart << 0, 0, 0, auvConstraints_->transJerk;
   transEnd << distance_, 0, 0, auvConstraints_->transJerk;

   // Rotation
   qEnd_ = wEnd_->quaternion();
   Eigen::Quaterniond qDiff = qStop_.conjugate() * qEnd_; // Error quaternion wrt B-frame (wrt I-frame: q2 * q1.conjugate)
   double angularDistance = auv_core::rot3d::quat2AngleAxis(qDiff)(0);

   Eigen::Vector4d rotStart = Eigen::Vector4d::Zero();
   Eigen::Vector4d rotEnd = Eigen::Vector4d::Zero();
   rotStart << 0, 0, 0, auvConstraints_->rotJerk;
   rotEnd << angularDistance, 0, 0, auvConstraints_->rotJerk;

   // Compute durations
   double timeTrans = 0, timeRot = 0;
   MinJerkTimeSolver *mjts;
   mjts = new MinJerkTimeSolver(transStart, transEnd);
   timeTrans = mjts->getTime();
   mjts = new MinJerkTimeSolver(rotStart, rotEnd);
   timeRot = mjts->getTime();

   simultaneousDuration_ = std::max(timeTrans, timeRot);                             // Take longer duration
   std::cout << "BT: simultaneous duration: " << simultaneousDuration_ << std::endl; // Debug
}

/**
 * \brief Set the primary trajectory to use (long or simultaneous)
 */
void BasicTrajectory::setPrimaryTrajectory()
{
   BasicTrajectory::computeMaxVelocityST();

   Eigen::Quaterniond qSlerp = qStop_.slerp(0.5, qEnd_);           // Quaternion at midpoint
   Eigen::Vector3d maxVelB = qSlerp.conjugate() * maxVelocityVec_; // Max vel xpressed in B-frame
   Eigen::Vector3d maxVelI = maxVelocityVec_;                      // Max vel Expressed in I-frame
   double distanceXY = deltaVec_.head<2>().norm();
   double distanceZ = fabs(deltaVec_(2));

   // Determine if XYZ distances are violated
   if (distanceXY > auvConstraints_->maxXYDistance)
   {
      isSimultaneousTrajectory_ = false;
      std::cout << "BT: MAX distance XY: " << distanceXY << " > " << auvConstraints_->maxXYDistance << std::endl; // Debug
   }
   if (distanceZ > auvConstraints_->maxZDistance)
   {
      isSimultaneousTrajectory_ = false;
      std::cout << "BT: MAX distance Z: " << distanceZ << " > " << auvConstraints_->maxZDistance << std::endl; // Debug
   }

   // Determine if XYZ velocities are violated
   for (int i = 0; i < 3; i++)
   {
      // Check inertial-frame velocity
      if (fabs(maxVelI(i)) > auvConstraints_->maxTransVel(i))
      {
         std::cout << "BT: MAX I-frame velocity " << i << "-axis: " << fabs(maxVelI(i)) << " > " << auvConstraints_->maxTransVel(i) << std::endl; // Debug
         isSimultaneousTrajectory_ = false;
         maxVelI(i) = auv_core::math_lib::sign(maxVelI(i)) * auvConstraints_->maxTransVel(i);
      }

      // Check body-frame velocity
      if (fabs(maxVelB(i)) > auvConstraints_->maxTransVel(i))
      {
         std::cout << "BT: MAX B-frame velocity " << i << "-axis: " << fabs(maxVelB(i)) << " > " << auvConstraints_->maxTransVel(i) << std::endl; // Debug
         isSimultaneousTrajectory_ = false;
         maxVelB(i) = auv_core::math_lib::sign(maxVelB(i)) * auvConstraints_->maxTransVel(i);
      }
   }

   // // Set the allowed max velocity
   maxVelocity_ = std::min(maxVelI.norm(), maxVelB.norm());

   if (!isSimultaneousTrajectory_) // Execute long trajectory
   {
      isLongTrajectory_ = true;
      double cruiseRatio = 1.0 - maxVelocity_ / maxVelocityST_;
      ltPrimary_ = new LongTrajectory(wStop_, wEnd_, auvConstraints_, cruiseRatio, maxVelocity_);
      longDuration_ = ltPrimary_->getTime();
      totalDuration_ += longDuration_;
   }
   else // Execute simultaneous trajectory
   {
      isSimultaneousTrajectory_ = true;
      stPrimary_ = new SimultaneousTrajectory(wStop_, wEnd_, simultaneousDuration_);
      totalDuration_ = simultaneousDuration_;
   }
   std::cout << "BT long trajectory [bool]: " << isLongTrajectory_ << std::endl;                 // Debug
   std::cout << "BT simultaneous trajectory [bool]: " << isSimultaneousTrajectory_ << std::endl; // Debug
}

/**
 * \brief Return the total duration
 */
double BasicTrajectory::getTime()
{
   return totalDuration_;
}

/**
 * @param time Time to compute the state at
 * \brief Computes the trajectory state at the specified time
 */
auv_core::Vector13d BasicTrajectory::computeState(double time)
{
   if (time <= stopDuration_)
   {
      //std::cout << "BT: stop trajectory at time " << time << std::endl; // Debug
      return stStop_->computeState(time);
   }
   else if (isSimultaneousTrajectory_)
   {
      //std::cout << "BT: main trajectory at time " << time - stopDuration_ << std::endl; // Debug
      return stPrimary_->computeState(time - stopDuration_);
   }
   else if (isLongTrajectory_)
   {
      return ltPrimary_->computeState(time - stopDuration_);
   }
}

/**
 * @param time Time to compute accelerations at
 * \brief Compute the trajectory acceleration at specified time (inertial translational acceleration and time-derivative of angular velocity), 
 * both expressed in B-frame.
 */
auv_core::Vector6d BasicTrajectory::computeAccel(double time)
{
   if (time <= stopDuration_)
   {
      return stStop_->computeAccel(time);
   }
   else if (isSimultaneousTrajectory_)
   {
      return stPrimary_->computeAccel(time - stopDuration_);
   }
   else if (isLongTrajectory_)
   {
      return ltPrimary_->computeAccel(time - stopDuration_);
   }
}
} // namespace auv_guidance