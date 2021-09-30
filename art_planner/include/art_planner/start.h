#pragma once

#include <ompl/base/goals/GoalState.h>
#include <ompl/util/RandomNumbers.h>



namespace ob = ompl::base;



class StartState : public ob::GoalState {

  unsigned int max_num_samples_{0};
  mutable ompl::RNG rng_;
  bool verbose_{false};

public:

  using ob::GoalState::GoalState;

  virtual void sampleGoal(ob::State *state) const override;

  void setMaxNumSamples(const unsigned int& num_samples);

  void setVerbosity(bool verbose);

};
