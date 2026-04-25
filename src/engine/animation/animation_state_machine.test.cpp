#include <gtest/gtest.h>
#include "engine/animation/animation_state_machine.hpp"

using namespace mmo::engine::animation;

class StateMachineTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a simple idle -> walk -> run state machine
        AnimState idle;
        idle.name = "idle";
        idle.clip_name = "idle_anim";
        idle.loop = true;
        StateTransition idle_to_walk;
        idle_to_walk.target_state = "walk";
        idle_to_walk.crossfade_duration = 0.2f;
        idle_to_walk.priority = 0;
        TransitionCondition speed_gt_half;
        speed_gt_half.param_name = "speed";
        speed_gt_half.op = TransitionCondition::Op::GT;
        speed_gt_half.threshold = 0.5f;
        idle_to_walk.conditions.push_back(speed_gt_half);
        idle.transitions.push_back(idle_to_walk);

        AnimState walk;
        walk.name = "walk";
        walk.clip_name = "walk_anim";
        walk.loop = true;
        StateTransition walk_to_idle;
        walk_to_idle.target_state = "idle";
        walk_to_idle.crossfade_duration = 0.2f;
        walk_to_idle.priority = 0;
        TransitionCondition speed_lt_half;
        speed_lt_half.param_name = "speed";
        speed_lt_half.op = TransitionCondition::Op::LT;
        speed_lt_half.threshold = 0.5f;
        walk_to_idle.conditions.push_back(speed_lt_half);
        walk.transitions.push_back(walk_to_idle);

        StateTransition walk_to_run;
        walk_to_run.target_state = "run";
        walk_to_run.crossfade_duration = 0.15f;
        walk_to_run.priority = 0;
        TransitionCondition speed_gt_two;
        speed_gt_two.param_name = "speed";
        speed_gt_two.op = TransitionCondition::Op::GT;
        speed_gt_two.threshold = 2.0f;
        walk_to_run.conditions.push_back(speed_gt_two);
        walk.transitions.push_back(walk_to_run);

        AnimState run;
        run.name = "run";
        run.clip_name = "run_anim";
        run.loop = true;
        StateTransition run_to_walk;
        run_to_walk.target_state = "walk";
        run_to_walk.crossfade_duration = 0.2f;
        run_to_walk.priority = 0;
        TransitionCondition speed_lt_two;
        speed_lt_two.param_name = "speed";
        speed_lt_two.op = TransitionCondition::Op::LT;
        speed_lt_two.threshold = 2.0f;
        run_to_walk.conditions.push_back(speed_lt_two);
        run.transitions.push_back(run_to_walk);

        sm.add_state(idle);
        sm.add_state(walk);
        sm.add_state(run);
        sm.set_default_state("idle");

        // Create minimal clips for binding
        AnimationClip clip_idle;
        clip_idle.name = "idle_anim";
        clip_idle.duration = 1.0f;
        AnimationClip clip_walk;
        clip_walk.name = "walk_anim";
        clip_walk.duration = 1.0f;
        AnimationClip clip_run;
        clip_run.name = "run_anim";
        clip_run.duration = 1.0f;

        std::vector<AnimationClip> clips = {clip_idle, clip_walk, clip_run};
        sm.bind_clips(clips);
    }

    AnimationStateMachine sm;
};

TEST_F(StateMachineTest, DefaultStateIsIdle) {
    EXPECT_EQ(sm.current_state(), "idle");
}

TEST_F(StateMachineTest, SetAndGetFloat) {
    sm.set_float("speed", 1.5f);
    EXPECT_FLOAT_EQ(sm.get_float("speed"), 1.5f);
}

TEST_F(StateMachineTest, SetAndGetBool) {
    sm.set_bool("attacking", true);
    EXPECT_TRUE(sm.get_bool("attacking"));
    sm.set_bool("attacking", false);
    EXPECT_FALSE(sm.get_bool("attacking"));
}

TEST_F(StateMachineTest, GetUnsetParamReturnsDefault) {
    EXPECT_FLOAT_EQ(sm.get_float("nonexistent"), 0.0f);
    EXPECT_FALSE(sm.get_bool("nonexistent"));
}

TEST_F(StateMachineTest, TransitionsWhenConditionMet) {
    sm.set_float("speed", 0.0f);
    AnimationPlayer player;
    sm.update(player);
    EXPECT_EQ(sm.current_state(), "idle");

    // Set speed above threshold -> should transition to walk
    sm.set_float("speed", 1.0f);
    sm.update(player);
    EXPECT_EQ(sm.current_state(), "walk");
}

TEST_F(StateMachineTest, NoTransitionWhenConditionNotMet) {
    sm.set_float("speed", 0.0f);
    AnimationPlayer player;
    sm.update(player);
    EXPECT_EQ(sm.current_state(), "idle");

    sm.set_float("speed", 0.3f);  // below 0.5 threshold
    sm.update(player);
    EXPECT_EQ(sm.current_state(), "idle");
}

TEST_F(StateMachineTest, TransitionChain) {
    AnimationPlayer player;
    sm.set_float("speed", 0.0f);
    sm.update(player);
    EXPECT_EQ(sm.current_state(), "idle");

    // idle -> walk
    sm.set_float("speed", 1.0f);
    sm.update(player);
    EXPECT_EQ(sm.current_state(), "walk");

    // walk -> run
    sm.set_float("speed", 3.0f);
    sm.update(player);
    EXPECT_EQ(sm.current_state(), "run");

    // run -> walk
    sm.set_float("speed", 1.0f);
    sm.update(player);
    EXPECT_EQ(sm.current_state(), "walk");

    // walk -> idle
    sm.set_float("speed", 0.0f);
    sm.update(player);
    EXPECT_EQ(sm.current_state(), "idle");
}

TEST_F(StateMachineTest, IsBoundAfterBindClips) {
    EXPECT_TRUE(sm.is_bound());
}

TEST_F(StateMachineTest, UnboundMachineIsNotBound) {
    AnimationStateMachine fresh;
    EXPECT_FALSE(fresh.is_bound());
}

// ============================================================================
// TransitionCondition
// ============================================================================

static std::vector<ParamValue> fparams(float v) {
    std::vector<ParamValue> out;
    out.push_back(ParamValue{v});
    return out;
}
static std::vector<ParamValue> bparams(bool v) {
    std::vector<ParamValue> out;
    out.push_back(ParamValue{v});
    return out;
}

TEST(TransitionCondition, EvaluateGT) {
    TransitionCondition cond;
    cond.param_index = 0;
    cond.op = TransitionCondition::Op::GT;
    cond.threshold = 1.0f;

    EXPECT_TRUE(cond.evaluate(fparams(2.0f)));
    EXPECT_FALSE(cond.evaluate(fparams(0.5f)));
}

TEST(TransitionCondition, EvaluateLT) {
    TransitionCondition cond;
    cond.param_index = 0;
    cond.op = TransitionCondition::Op::LT;
    cond.threshold = 1.0f;

    EXPECT_TRUE(cond.evaluate(fparams(0.5f)));
    EXPECT_FALSE(cond.evaluate(fparams(1.5f)));
}

TEST(TransitionCondition, EvaluateIsTrue) {
    TransitionCondition cond;
    cond.param_index = 0;
    cond.op = TransitionCondition::Op::IS_TRUE;

    EXPECT_TRUE(cond.evaluate(bparams(true)));
    EXPECT_FALSE(cond.evaluate(bparams(false)));
}

TEST(TransitionCondition, EvaluateIsFalse) {
    TransitionCondition cond;
    cond.param_index = 0;
    cond.op = TransitionCondition::Op::IS_FALSE;

    EXPECT_TRUE(cond.evaluate(bparams(false)));
    EXPECT_FALSE(cond.evaluate(bparams(true)));
}

TEST(TransitionCondition, InvalidIndexReturnsFalse) {
    TransitionCondition cond;
    cond.param_index = -1;
    cond.op = TransitionCondition::Op::GT;
    cond.threshold = 0.0f;

    EXPECT_FALSE(cond.evaluate(fparams(1.0f)));
}

TEST(TransitionCondition, EvaluateEQ) {
    TransitionCondition cond;
    cond.param_index = 0;
    cond.op = TransitionCondition::Op::EQ;
    cond.threshold = 5.0f;

    EXPECT_TRUE(cond.evaluate(fparams(5.0f)));
    EXPECT_TRUE(cond.evaluate(fparams(5.0005f)));
    EXPECT_FALSE(cond.evaluate(fparams(5.01f)));
    EXPECT_FALSE(cond.evaluate(fparams(4.0f)));
}

TEST(TransitionCondition, EvaluateNE) {
    TransitionCondition cond;
    cond.param_index = 0;
    cond.op = TransitionCondition::Op::NE;
    cond.threshold = 5.0f;

    EXPECT_FALSE(cond.evaluate(fparams(5.0f)));
    EXPECT_FALSE(cond.evaluate(fparams(5.0005f)));
    EXPECT_TRUE(cond.evaluate(fparams(6.0f)));
    EXPECT_TRUE(cond.evaluate(fparams(0.0f)));
}

// ============================================================================
// Edge cases: multiple conditions, priority, bind_clips, etc.
// ============================================================================

TEST(StateMachineEdgeCases, MultipleConditionsAllPass) {
    // Two conditions on a single transition: both must pass (AND logic)
    AnimationStateMachine sm;

    AnimState idle;
    idle.name = "idle";
    idle.clip_name = "idle_anim";
    idle.loop = true;

    StateTransition to_attack;
    to_attack.target_state = "attack";
    to_attack.crossfade_duration = 0.1f;

    TransitionCondition speed_gt;
    speed_gt.param_name = "speed";
    speed_gt.op = TransitionCondition::Op::GT;
    speed_gt.threshold = 0.5f;

    TransitionCondition stamina_gt;
    stamina_gt.param_name = "stamina";
    stamina_gt.op = TransitionCondition::Op::GT;
    stamina_gt.threshold = 10.0f;

    to_attack.conditions.push_back(speed_gt);
    to_attack.conditions.push_back(stamina_gt);
    idle.transitions.push_back(to_attack);

    AnimState attack;
    attack.name = "attack";
    attack.clip_name = "attack_anim";
    attack.loop = false;

    sm.add_state(idle);
    sm.add_state(attack);
    sm.set_default_state("idle");

    AnimationClip c1; c1.name = "idle_anim"; c1.duration = 1.0f;
    AnimationClip c2; c2.name = "attack_anim"; c2.duration = 1.0f;
    sm.bind_clips({c1, c2});

    AnimationPlayer player;

    // Both conditions met
    sm.set_float("speed", 1.0f);
    sm.set_float("stamina", 20.0f);
    sm.update(player);
    EXPECT_EQ(sm.current_state(), "attack");
}

TEST(StateMachineEdgeCases, MultipleConditionsOneFailsNoTransition) {
    AnimationStateMachine sm;

    AnimState idle;
    idle.name = "idle";
    idle.clip_name = "idle_anim";
    idle.loop = true;

    StateTransition to_attack;
    to_attack.target_state = "attack";

    TransitionCondition speed_gt;
    speed_gt.param_name = "speed";
    speed_gt.op = TransitionCondition::Op::GT;
    speed_gt.threshold = 0.5f;

    TransitionCondition stamina_gt;
    stamina_gt.param_name = "stamina";
    stamina_gt.op = TransitionCondition::Op::GT;
    stamina_gt.threshold = 10.0f;

    to_attack.conditions.push_back(speed_gt);
    to_attack.conditions.push_back(stamina_gt);
    idle.transitions.push_back(to_attack);

    AnimState attack;
    attack.name = "attack";
    attack.clip_name = "attack_anim";
    attack.loop = false;

    sm.add_state(idle);
    sm.add_state(attack);
    sm.set_default_state("idle");

    AnimationClip c1; c1.name = "idle_anim"; c1.duration = 1.0f;
    AnimationClip c2; c2.name = "attack_anim"; c2.duration = 1.0f;
    sm.bind_clips({c1, c2});

    AnimationPlayer player;

    // Speed passes but stamina does not
    sm.set_float("speed", 1.0f);
    sm.set_float("stamina", 5.0f);
    sm.update(player);
    EXPECT_EQ(sm.current_state(), "idle");
}

TEST(StateMachineEdgeCases, PriorityOrderingHigherWins) {
    // Two transitions could fire; higher priority should win
    AnimationStateMachine sm;

    AnimState idle;
    idle.name = "idle";
    idle.clip_name = "idle_anim";
    idle.loop = true;

    StateTransition to_walk;
    to_walk.target_state = "walk";
    to_walk.priority = 0;
    TransitionCondition speed_gt_low;
    speed_gt_low.param_name = "speed";
    speed_gt_low.op = TransitionCondition::Op::GT;
    speed_gt_low.threshold = 0.1f;
    to_walk.conditions.push_back(speed_gt_low);

    StateTransition to_run;
    to_run.target_state = "run";
    to_run.priority = 10;  // higher priority
    TransitionCondition speed_gt_high;
    speed_gt_high.param_name = "speed";
    speed_gt_high.op = TransitionCondition::Op::GT;
    speed_gt_high.threshold = 0.1f;
    to_run.conditions.push_back(speed_gt_high);

    idle.transitions.push_back(to_walk);
    idle.transitions.push_back(to_run);

    AnimState walk;
    walk.name = "walk";
    walk.clip_name = "walk_anim";
    walk.loop = true;

    AnimState run;
    run.name = "run";
    run.clip_name = "run_anim";
    run.loop = true;

    sm.add_state(idle);
    sm.add_state(walk);
    sm.add_state(run);
    sm.set_default_state("idle");

    AnimationClip c1; c1.name = "idle_anim"; c1.duration = 1.0f;
    AnimationClip c2; c2.name = "walk_anim"; c2.duration = 1.0f;
    AnimationClip c3; c3.name = "run_anim"; c3.duration = 1.0f;
    sm.bind_clips({c1, c2, c3});

    AnimationPlayer player;
    sm.set_float("speed", 1.0f);
    sm.update(player);

    // Higher priority transition (to_run, priority=10) should win
    EXPECT_EQ(sm.current_state(), "run");
}

TEST(StateMachineEdgeCases, BoolParameterTransitionsIsTrueIsFalse) {
    // Full state machine test with IS_TRUE / IS_FALSE conditions
    AnimationStateMachine sm;

    AnimState idle;
    idle.name = "idle";
    idle.clip_name = "idle_anim";
    idle.loop = true;
    StateTransition to_attack;
    to_attack.target_state = "attack";
    TransitionCondition attacking_true;
    attacking_true.param_name = "attacking";
    attacking_true.op = TransitionCondition::Op::IS_TRUE;
    to_attack.conditions.push_back(attacking_true);
    idle.transitions.push_back(to_attack);

    AnimState attack;
    attack.name = "attack";
    attack.clip_name = "attack_anim";
    attack.loop = true;
    StateTransition to_idle;
    to_idle.target_state = "idle";
    TransitionCondition attacking_false;
    attacking_false.param_name = "attacking";
    attacking_false.op = TransitionCondition::Op::IS_FALSE;
    to_idle.conditions.push_back(attacking_false);
    attack.transitions.push_back(to_idle);

    sm.add_state(idle);
    sm.add_state(attack);
    sm.set_default_state("idle");

    AnimationClip c1; c1.name = "idle_anim"; c1.duration = 1.0f;
    AnimationClip c2; c2.name = "attack_anim"; c2.duration = 1.0f;
    sm.bind_clips({c1, c2});

    AnimationPlayer player;

    // Initially "attacking" param doesn't exist yet as bool; default is float 0.0
    // IS_TRUE on a float should fail -> stay in idle
    sm.update(player);
    EXPECT_EQ(sm.current_state(), "idle");

    // Set attacking to true -> should transition
    sm.set_bool("attacking", true);
    sm.update(player);
    EXPECT_EQ(sm.current_state(), "attack");

    // Set attacking to false -> should go back
    sm.set_bool("attacking", false);
    sm.update(player);
    EXPECT_EQ(sm.current_state(), "idle");
}

TEST(StateMachineEdgeCases, SetFloatOverwritesBoolParam) {
    // Setting a float on a param that was previously bool should change the type
    AnimationStateMachine sm;
    AnimState idle;
    idle.name = "idle";
    idle.clip_name = "idle_anim";
    sm.add_state(idle);
    sm.set_default_state("idle");

    AnimationClip c; c.name = "idle_anim"; c.duration = 1.0f;
    sm.bind_clips({c});

    sm.set_bool("param", true);
    EXPECT_TRUE(sm.get_bool("param"));

    sm.set_float("param", 3.14f);
    EXPECT_FLOAT_EQ(sm.get_float("param"), 3.14f);
    // After overwriting with float, get_bool should return default (false)
    EXPECT_FALSE(sm.get_bool("param"));
}

TEST(StateMachineEdgeCases, BindClipsMissingClipReturnsFalse) {
    AnimationStateMachine sm;

    AnimState s;
    s.name = "idle";
    s.clip_name = "nonexistent_clip";
    sm.add_state(s);
    sm.set_default_state("idle");

    // No clips provided -> clip not found
    std::vector<AnimationClip> empty_clips;
    EXPECT_FALSE(sm.bind_clips(empty_clips));
}

TEST(StateMachineEdgeCases, BindClipsMatchingClipsReturnsTrue) {
    AnimationStateMachine sm;

    AnimState s;
    s.name = "idle";
    s.clip_name = "idle_anim";
    sm.add_state(s);
    sm.set_default_state("idle");

    AnimationClip c; c.name = "idle_anim"; c.duration = 1.0f;
    EXPECT_TRUE(sm.bind_clips({c}));
    EXPECT_TRUE(sm.is_bound());
}

TEST(StateMachineEdgeCases, UpdateBeforeBindDoesNotCrash) {
    AnimationStateMachine sm;

    AnimState idle;
    idle.name = "idle";
    idle.clip_name = "idle_anim";
    idle.loop = true;
    sm.add_state(idle);
    sm.set_default_state("idle");

    AnimationPlayer player;

    // Should not crash -- machine is not bound
    EXPECT_FALSE(sm.is_bound());
    sm.set_float("speed", 5.0f);
    sm.update(player);

    // State should be empty since we never bound
    EXPECT_EQ(sm.current_state(), "");
}

TEST(StateMachineEdgeCases, RapidParameterChangesOnlyFinalValueMatters) {
    AnimationStateMachine sm;

    AnimState idle;
    idle.name = "idle";
    idle.clip_name = "idle_anim";
    idle.loop = true;
    StateTransition to_walk;
    to_walk.target_state = "walk";
    TransitionCondition speed_gt;
    speed_gt.param_name = "speed";
    speed_gt.op = TransitionCondition::Op::GT;
    speed_gt.threshold = 1.0f;
    to_walk.conditions.push_back(speed_gt);
    idle.transitions.push_back(to_walk);

    AnimState walk;
    walk.name = "walk";
    walk.clip_name = "walk_anim";
    walk.loop = true;

    sm.add_state(idle);
    sm.add_state(walk);
    sm.set_default_state("idle");

    AnimationClip c1; c1.name = "idle_anim"; c1.duration = 1.0f;
    AnimationClip c2; c2.name = "walk_anim"; c2.duration = 1.0f;
    sm.bind_clips({c1, c2});

    AnimationPlayer player;

    // Set speed high, then low, in the same frame before update
    sm.set_float("speed", 5.0f);
    sm.set_float("speed", 0.1f);  // final value is below threshold
    sm.update(player);
    EXPECT_EQ(sm.current_state(), "idle");

    // Now set low then high -- final value is above threshold
    sm.set_float("speed", 0.0f);
    sm.set_float("speed", 2.0f);
    sm.update(player);
    EXPECT_EQ(sm.current_state(), "walk");
}

TEST(StateMachineEdgeCases, CurrentStateAfterMultipleRapidTransitions) {
    // Build a chain: A -> B -> C, where each transition fires immediately
    AnimationStateMachine sm;

    AnimState a;
    a.name = "A";
    a.clip_name = "clip_a";
    a.loop = true;
    StateTransition a_to_b;
    a_to_b.target_state = "B";
    TransitionCondition go_cond;
    go_cond.param_name = "go";
    go_cond.op = TransitionCondition::Op::IS_TRUE;
    a_to_b.conditions.push_back(go_cond);
    a.transitions.push_back(a_to_b);

    AnimState b;
    b.name = "B";
    b.clip_name = "clip_b";
    b.loop = true;
    StateTransition b_to_c;
    b_to_c.target_state = "C";
    TransitionCondition go_cond2;
    go_cond2.param_name = "go";
    go_cond2.op = TransitionCondition::Op::IS_TRUE;
    b_to_c.conditions.push_back(go_cond2);
    b.transitions.push_back(b_to_c);

    AnimState c;
    c.name = "C";
    c.clip_name = "clip_c";
    c.loop = true;

    sm.add_state(a);
    sm.add_state(b);
    sm.add_state(c);
    sm.set_default_state("A");

    AnimationClip ca; ca.name = "clip_a"; ca.duration = 1.0f;
    AnimationClip cb; cb.name = "clip_b"; cb.duration = 1.0f;
    AnimationClip cc; cc.name = "clip_c"; cc.duration = 1.0f;
    sm.bind_clips({ca, cb, cc});

    AnimationPlayer player;
    sm.set_bool("go", true);

    // Each update() only fires one transition per call
    sm.update(player);
    EXPECT_EQ(sm.current_state(), "B");

    sm.update(player);
    EXPECT_EQ(sm.current_state(), "C");

    // C has no transitions, so further updates stay in C
    sm.update(player);
    EXPECT_EQ(sm.current_state(), "C");
}
