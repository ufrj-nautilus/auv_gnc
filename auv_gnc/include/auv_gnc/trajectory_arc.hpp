#ifndef TRAJECTORY_ARC
#define TRAJECTORY_ARC

#include "auv_gnc/trajectory_generator_abstract.hpp"
#include "auv_gnc/segment_planner.hpp"
#include "eigen3/Eigen/Dense"
#include "math.h"

using namespace Eigen;

namespace AUV_GNC
{
namespace Translation
{
typedef Matrix<float, 3, 2> Matrix32f;

// Creates an arc segment in space using the SegmentPlanner for position/speed along the segment.
// Assumes the speed is either zero or equal to the speed entered in the constructor
class Arc : public TrajectoryGenerator
{
private:
    SegmentPlanner *segPlanner_;
    float cruiseSpeed_, acceleration_;
    int accelSeq_;
    Vector3f initialPos_, unitTangent_, unitNormal_, unitAxis_;
    float radius_, revAngle_;
    Matrix32f insertionMap_;
public:
    static const float DEFAULT_SPEED = 0.5; // [m/s]
    static const float DEFAULT_ACCEL = 0.2; // [m/s^2]

    Arc(const Ref<const Vector3f> initialPos, const Ref<const Vector3f> unitTangent, const Ref<const Vector3f> unitNormal,
         float radius, float revAngle, float nominalSpeed = Arc::DEFAULT_SPEED, float acceleration = 0.0, int seq = SegmentPlanner::SEQ_NONE);
    float getTravelTime();
    Vector12f computeState(float time);
};
}
}

#endif