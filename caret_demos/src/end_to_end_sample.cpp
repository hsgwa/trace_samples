#include <chrono>
#include <memory>
#include <vector>
#include <random>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"

#define QOS_HISTORY_SIZE 1

using namespace std::chrono_literals;

std::chrono::milliseconds lognormal_distribution()
{
  static std::random_device seed_gen;
  static std::default_random_engine engine(seed_gen());
  static std::lognormal_distribution<> dist(1.5, 1.7);
  static double max_latency_ms = 150;

  // average is about 15~20ms
  int sleep_ms = std::min(dist(engine), max_latency_ms);
  return std::chrono::milliseconds(sleep_ms);
}

class TimerDependencyNode : public rclcpp::Node
{
public:
  TimerDependencyNode(
    std::string node_name, std::string sub_topic_name, std::string pub_topic_name,
    int period_ms)
  : Node(node_name)
  {
    pub_ = create_publisher<sensor_msgs::msg::Image>(pub_topic_name, QOS_HISTORY_SIZE);
    sub_ = create_subscription<sensor_msgs::msg::Image>(
      sub_topic_name, QOS_HISTORY_SIZE,
      [&](sensor_msgs::msg::Image::UniquePtr msg)
      {
        rclcpp::sleep_for(lognormal_distribution());
        msg_ = std::move(msg);
      });


    timer_ = create_wall_timer(
      std::chrono::milliseconds(period_ms), [&]()
      {
        rclcpp::sleep_for(lognormal_distribution());
        if (msg_) {
          pub_->publish(*msg_);
        }
      });
  }

private:
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub_;
  sensor_msgs::msg::Image::UniquePtr msg_;
  rclcpp::TimerBase::SharedPtr timer_;
};

class ActuatorDummy : public rclcpp::Node
{
public:
  ActuatorDummy(std::string node_name, std::string sub_topic_name)
  : Node(node_name)
  {
    sub_ = create_subscription<sensor_msgs::msg::Image>(
      sub_topic_name, QOS_HISTORY_SIZE,
      [&](sensor_msgs::msg::Image::UniquePtr msg)
      {
        (void)msg;
      }
    );
  }

private:
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub_;
};

class NoDependencyNode : public rclcpp::Node
{
public:
  NoDependencyNode(std::string node_name, std::string sub_topic_name, std::string pub_topic_name)
  : Node(node_name)
  {
    pub_ = create_publisher<sensor_msgs::msg::Image>(pub_topic_name, QOS_HISTORY_SIZE);
    sub_ = create_subscription<sensor_msgs::msg::Image>(
      sub_topic_name, QOS_HISTORY_SIZE, [&](sensor_msgs::msg::Image::UniquePtr msg)
      {
        rclcpp::sleep_for(lognormal_distribution());
        pub_->publish(std::move(msg));
      });
  }

private:
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub_;
};

class SubDependencyNode : public rclcpp::Node
{
public:
  SubDependencyNode(
    std::string node_name,
    std::string sub_topic_name,
    std::string subsequent_sub_topic_name,
    std::string pub_topic_name
  )
  : Node(node_name)
  {
    sub1_ = create_subscription<sensor_msgs::msg::Image>(
      sub_topic_name, QOS_HISTORY_SIZE, [&](sensor_msgs::msg::Image::UniquePtr msg)
      {
        auto msg_tmp = std::make_unique<sensor_msgs::msg::Image>();
        msg_tmp->header.stamp = msg->header.stamp;
        rclcpp::sleep_for(lognormal_distribution());
        msg_ = std::move(msg);
      });
    sub2_ = create_subscription<sensor_msgs::msg::Image>(
      subsequent_sub_topic_name, QOS_HISTORY_SIZE, [&](sensor_msgs::msg::Image::UniquePtr msg)
      {
        (void)msg;
        rclcpp::sleep_for(lognormal_distribution());
        if (msg_) {
          pub_->publish(std::move(msg_));
        }
      });
    pub_ = create_publisher<sensor_msgs::msg::Image>(pub_topic_name, QOS_HISTORY_SIZE);
  }

private:
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub1_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub2_;
  sensor_msgs::msg::Image::UniquePtr msg_;
};

class SensorDummy : public rclcpp::Node
{
public:
  SensorDummy(std::string node_name, std::string topic_name, int period_ms)
  : Node(node_name)
  {
    auto period = std::chrono::milliseconds(period_ms);

    auto callback = [&]() {
        auto msg = std::make_unique<sensor_msgs::msg::Image>();
        msg->header.stamp = now();
        pub_->publish(std::move(msg));
      };
    pub_ = create_publisher<sensor_msgs::msg::Image>(topic_name, 1);
    timer_ = create_wall_timer(period, callback);
  }

private:
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  auto executor = std::make_shared<rclcpp::executors::MultiThreadedExecutor>();

  std::vector<std::shared_ptr<rclcpp::Node>> nodes;

  nodes.emplace_back(std::make_shared<ActuatorDummy>("actuator_dummy_node", "/topic4"));
  nodes.emplace_back(
    std::make_shared<NoDependencyNode>("filter_node", "/topic1", "/topic2"));
  nodes.emplace_back(
    std::make_shared<SubDependencyNode>("message_driven_node", "/topic2", "/drive", "/topic3"));
  nodes.emplace_back(
    std::make_shared<TimerDependencyNode>("timer_driven_node", "/topic3", "/topic4", 100)); // 10Hz
  nodes.emplace_back(std::make_shared<SensorDummy>("sensor_dummy_node", "/topic1", 50)); // 20Hz
  nodes.emplace_back(std::make_shared<SensorDummy>("drive_node", "/drive", 100)); // 10Hz

  for (auto & node : nodes) {
    executor->add_node(node);
  }

  executor->spin();
  rclcpp::shutdown();

  return 0;
}
