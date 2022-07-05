/*
 * Copyright (C) 2022 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

#ifndef SRC__LANE_BLOCKER__LANEBLOCKER_HPP
#define SRC__LANE_BLOCKER__LANEBLOCKER_HPP

#include <rclcpp/rclcpp.hpp>

#include <rmf_traffic/agv/Graph.hpp>

#include <rmf_obstacle_msgs/msg/obstacles.hpp>
#include <rmf_building_map_msgs/msg/graph.hpp>
#include <rmf_fleet_msgs/msg/lane_request.hpp>
#include <rmf_fleet_msgs/msg/speed_limit_request.hpp>
#include <rmf_fleet_msgs/msg/lane_states.hpp>

#include <vision_msgs/msg/bounding_box3_d.hpp>

#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>

#include <unordered_map>
#include <unordered_set>
#include <functional>

//==============================================================================
/// Modify states of lanes for fleet adapters based on density of obstacles
class LaneBlocker : public rclcpp::Node
{
public:
  using Obstacles = rmf_obstacle_msgs::msg::Obstacles;
  using Obstacle = rmf_obstacle_msgs::msg::Obstacle;
  using NavGraph = rmf_building_map_msgs::msg::Graph;
  using TrafficGraph = rmf_traffic::agv::Graph;
  using LaneRequest = rmf_fleet_msgs::msg::LaneRequest;
  using SpeedLimitRequest = rmf_fleet_msgs::msg::SpeedLimitRequest;
  using LaneStates = rmf_fleet_msgs::msg::LaneStates;
  using BoundingBox = vision_msgs::msg::BoundingBox3D;
  using Header = std_msgs::msg::Header;

  /// Constructor
  LaneBlocker(
    const rclcpp::NodeOptions& options  = rclcpp::NodeOptions());

private:
    void obstacle_cb(const Obstacles& msg);
    void process();
    void cull();

    struct ObstacleData
    {
      rclcpp::Time expiry_time;
      std::size_t id;
      std::string source;
      BoundingBox transformed_bbox;

      ObstacleData(
        rclcpp::Time expiry_time_,
        std::size_t id_,
        const std::string& source_,
        BoundingBox transformed_bbox_)
      : expiry_time(expiry_time_),
        id(id_),
        source(std::move(source_)),
        transformed_bbox(std::move(transformed_bbox_))
      { }

      // Overload == for hashing
      inline bool operator==(const ObstacleData& other)
      const
      {
        const auto lhs_key = LaneBlocker::get_obstacle_key(source, id);
        const auto rhs_key = LaneBlocker::get_obstacle_key(other.source, other.id);
        return lhs_key == rhs_key;
      }
    };
    using ObstacleDataConstSharedPtr = std::shared_ptr<const ObstacleData>;

    static inline std::string get_obstacle_key(
      const std::string& source, const std::size_t id)
    {
      return source + "_" + std::to_string(id);
    }

    static inline std::string get_obstacle_key(const ObstacleData& obstacle)
    {
      return LaneBlocker::get_obstacle_key(
        obstacle.source, obstacle.id);
    }

    static inline std::string get_lane_key(
      const std::string& fleet_name,
      const std::size_t lane_index)
    {
      return fleet_name + "_" + std::to_string(lane_index);
    }

    struct ObstacleHash
    {
      std::size_t operator()(
        const ObstacleDataConstSharedPtr& obstacle) const
      {
        const std::string key = LaneBlocker::get_obstacle_key(*obstacle);
        return std::hash<std::string>()(key);
      }
    };

    std::pair<std::string, std::size_t>
    deserialize_key(const std::string& key) const;

    const TrafficGraph::Lane& lane_from_key(const std::string& key) const;

    // Modify lanes with changes in number of vicinity obstacles
    void request_lane_modifications(
      const std::unordered_set<std::string>& changes);

    // Store obstacle after transformation into RMF frame.
    // Generate key using get_obstacle_key()
    // We cache them based on source + id so that we keep only the latest
    // version of that obstacle.
    std::unordered_map<std::string, ObstacleDataConstSharedPtr>
    _obstacle_buffer = {};

    // TODO(YV): Based on the current implementation, we should be able to
    // cache obstacle_key directly
    // Map an obstacle to the lanes in its vicinity
    std::unordered_map<
      ObstacleDataConstSharedPtr,
      std::unordered_set<std::string>,
      ObstacleHash> _obstacle_to_lanes_map = {};

    // Map lane to a set of obstacles in its vicinity. This is only used to
    // check the number of obstacles in the vicinity of a lane. The obstacles
    // are represented as their obstacle keys.
    std::unordered_map<
      std::string,
      std::unordered_set<std::string>>
      _lane_to_obstacles_map = {};

    std::unordered_set<std::string> _currently_closed_lanes;

    rclcpp::Subscription<Obstacles>::SharedPtr _obstacle_sub;
    rclcpp::Subscription<NavGraph>::SharedPtr _graph_sub;
    rclcpp::Subscription<LaneStates>::SharedPtr _lane_states_sub;
    rclcpp::Publisher<LaneRequest>::SharedPtr _lane_closure_pub;
    rclcpp::Publisher<SpeedLimitRequest>::SharedPtr _speed_limit_pub;
    double _tf2_lookup_duration;

    std::string _rmf_frame;
    std::unique_ptr<tf2_ros::Buffer> _tf2_buffer;
    std::shared_ptr<tf2_ros::TransformListener> _transform_listener;

    std::unordered_map<std::string, TrafficGraph> _traffic_graphs;
    std::unordered_map<std::string, LaneStates::ConstSharedPtr> _lane_states;
    double _lane_width;
    double _obstacle_lane_threshold;
    std::chrono::nanoseconds _max_search_duration;
    std::size_t _lane_closure_threshold;

    rclcpp::TimerBase::SharedPtr _process_timer;
    rclcpp::TimerBase::SharedPtr _cull_timer;
};

#endif // SRC__LANE_BLOCKER__LANEBLOCKER_HPP