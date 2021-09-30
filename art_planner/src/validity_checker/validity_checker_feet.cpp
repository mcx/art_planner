#include "art_planner/validity_checker/validity_checker_feet.h"

#include <grid_map_core/iterators/PolygonIterator.hpp>

#include "art_planner/utils.h"



using namespace art_planner;



// TODO: Probably better to make this class member.
static const unsigned int kNLongSplit = 3;
static const unsigned int kNLatSplit = 3;

static double kCostCenter;
static double kCostLon;
static double kCostLat;
static double kCostDiag;
static double kMaxCost;



ValidityCheckerFeet::ValidityCheckerFeet(const ParamsConstPtr& params) : params_(params) {
  if (params_->objectives.clearance.enable) {
    box_length_ = params_->robot.feet.reach.x / kNLongSplit;
    box_width_ = params_->robot.feet.reach.y / kNLatSplit;

    kCostCenter = params_->objectives.clearance.cost_center;
    kCostLon = params_->objectives.clearance.cost_lon;
    kCostLat = params_->objectives.clearance.cost_lat;
    kCostDiag = params_->objectives.clearance.cost_diag;
    kMaxCost = 4*(kCostCenter + 2*kCostLon + 2*kCostLat + 4*kCostDiag);
  } else {
    box_length_ = params_->robot.feet.reach.x;
    box_width_ = params_->robot.feet.reach.y;
  }

  checker_.reset(new HeightMapBoxChecker(box_length_, box_width_, params_->robot.feet.reach.z));
}



void ValidityCheckerFeet::setMap(const MapPtr &map) {
  traversability_map_ = map;
}



static HeightMapBoxChecker::dPose d_pose;



bool ValidityCheckerFeet::boxIsValidAtPose(const Pose3 &pose) const {
  // Return if outside of map bounds.
  if (!traversability_map_->isInside(grid_map::Position(pose.translation().x(),
                                                        pose.translation().y()))) {
    return !params_->planner.unknown_space_untraversable;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  d_pose.origin[0] = pose.translation().x();
  d_pose.origin[1] = pose.translation().y();
  d_pose.origin[2] = pose.translation().z();
  Eigen::Map<Eigen::Matrix<dReal, 3, 4,Eigen::RowMajor> > rot(d_pose.rotation.data());
  rot.topLeftCorner(3,3) = pose.matrix().topLeftCorner(3,3);
  return static_cast<bool>(checker_->checkCollision({d_pose}));
}



bool ValidityCheckerFeet::boxesAreValidAtPoses(const std::vector<Pose3> &poses) const {
  bool valid = true;
  for (const auto& pose: poses) {
    valid &= boxIsValidAtPose(pose);
    // Need all four feet to be valid, so we can return, when one is not.
    if (!valid) break;
  }
  return valid;
}



double ValidityCheckerFeet::getClearance(const Pose3& pose) const {
  const std::vector<Pose3> foot_poses({
                     pose*Pose3FromXYZ( params_->robot.feet.offset.x,  params_->robot.feet.offset.y, 0.0f),
                     pose*Pose3FromXYZ( params_->robot.feet.offset.x, -params_->robot.feet.offset.y, 0.0f),
                     pose*Pose3FromXYZ(-params_->robot.feet.offset.x,  params_->robot.feet.offset.y, 0.0f),
                     pose*Pose3FromXYZ(-params_->robot.feet.offset.x, -params_->robot.feet.offset.y, 0.0f)});
  double cost = 0;
  for (const auto& foot_pose: foot_poses) {
    // Check center.
    if (!boxIsValidAtPose(foot_pose)) cost += kCostCenter;
    // Check longitudinal.
    if (!boxIsValidAtPose(foot_pose*Pose3FromXYZ(box_length_, 0.0f, 0.0f))) cost += kCostLon;
    if (!boxIsValidAtPose(foot_pose*Pose3FromXYZ(-box_length_, 0.0f, 0.0f))) cost += kCostLon;
    // Check lateral.
    if (!boxIsValidAtPose(foot_pose*Pose3FromXYZ(0.0f, box_width_, 0.0f))) cost += kCostLat;
    if (!boxIsValidAtPose(foot_pose*Pose3FromXYZ(0.0f, -box_width_, 0.0f))) cost += kCostLat;
    // Check diagnoal.
    if (!boxIsValidAtPose(foot_pose*Pose3FromXYZ(box_length_, box_width_, 0.0f))) cost += kCostDiag;
    if (!boxIsValidAtPose(foot_pose*Pose3FromXYZ(box_length_, -box_width_, 0.0f))) cost += kCostDiag;
    if (!boxIsValidAtPose(foot_pose*Pose3FromXYZ(-box_length_, box_width_, 0.0f))) cost += kCostDiag;
    if (!boxIsValidAtPose(foot_pose*Pose3FromXYZ(-box_length_, -box_width_, 0.0f))) cost += kCostDiag;
  }

  return kMaxCost-cost;
}



bool ValidityCheckerFeet::isValid(const Pose3& pose) const {
  std::vector<Pose3> foot_poses({
                     pose*Pose3FromXYZ( params_->robot.feet.offset.x,  params_->robot.feet.offset.y, 0.0f),
                     pose*Pose3FromXYZ( params_->robot.feet.offset.x, -params_->robot.feet.offset.y, 0.0f),
                     pose*Pose3FromXYZ(-params_->robot.feet.offset.x,  params_->robot.feet.offset.y, 0.0f),
                     pose*Pose3FromXYZ(-params_->robot.feet.offset.x, -params_->robot.feet.offset.y, 0.0f)});
  if (params_->objectives.clearance.enable) {
    int long_mult;
    int lat_mult;
    bool foot_valid;
    for (const auto& foot_pose: foot_poses) {
      long_mult = -1;
      foot_valid = false;
      for (unsigned int i = 0; i < kNLongSplit; ++i) {
        lat_mult = -1;
        for (unsigned int j = 0; j < kNLatSplit; ++j) {
          foot_valid |= boxIsValidAtPose(foot_pose*Pose3FromXYZ(box_length_*long_mult,
                                                                box_width_*lat_mult,
                                                                0.0f));
          // Escape foot loop if one box is in contact.
          if (foot_valid) break;
          ++lat_mult;
        }
        if (foot_valid) break;
        ++long_mult;
      }
      if (!foot_valid) return false;  // If one foot is not valid, entire pose is not.
    }
    return true;
  } else {
    return boxesAreValidAtPoses(foot_poses);
  }
}



bool ValidityCheckerFeet::hasMap() const {
  return static_cast<bool>(traversability_map_);
}

void ValidityCheckerFeet::updateHeightField() {
  std::lock_guard<std::mutex> lock(mutex_);
  checker_->setHeightField(traversability_map_, "elevation_masked");
}
