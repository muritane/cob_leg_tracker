#include <ros/ros.h>
#include <sensor_msgs/LaserScan.h>
#include <algorithm>
#include <vector>
#include "std_msgs/String.h"
#include <laser_geometry/laser_geometry.h>
#include <sensor_msgs/PointCloud.h>
#include <sensor_msgs/PointCloud2.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <tf2_ros/transform_listener.h>
#include <geometry_msgs/TransformStamped.h>
#include <tf2/convert.h>
#include <tf2_ros/buffer.h>
#include <tf2/transform_datatypes.h>
#include <tf2_sensor_msgs/tf2_sensor_msgs.h>
#include <pcl_ros/point_cloud.h>
#include <pcl/filters/passthrough.h>
#include "opencv2/core/version.hpp"
#include <opencv2/highgui/highgui.hpp>
#include <pcl/sample_consensus/ransac.h>
#include <pcl/sample_consensus/sac_model_circle.h>
#include <pcl/ModelCoefficients.h>
#include <pcl/sample_consensus/method_types.h>
#include <pcl/sample_consensus/model_types.h>
#include <pcl/sample_consensus/sac.h>
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>
#include <pcl/filters/radius_outlier_removal.h>
#include <iostream>
#include <iirob_filters/kalman_filter.h>
#include <pcl/common/centroid.h>
#include <pcl/common/common.h>
#include <iirob_filters/KalmanFilterParameters.h>
#include <std_msgs/Float64MultiArray.h>
#include <pcl/kdtree/kdtree.h>
#include <pcl/segmentation/extract_clusters.h>
#include <leg.h>
#include <math.h>
#include <Eigen/Geometry>
#include <Eigen/Eigenvalues>
#include <person.h>
#include <cstdlib>
//#include "Matrix.h"
#include <munkres.h>
#include "nav_msgs/OccupancyGrid.h"
#include <fstream>
#include "geometry_msgs/PointStamped.h"
#include "tf2_geometry_msgs/tf2_geometry_msgs.h"





/*
 * TODO:
 * N1 ~ new people appeared
 * N2 ~ continue tracking of already tracked people which were occluded
 * N1 = 0.7 and N2 = 1.2
 *
 * Invarianten erstellen:
 * if scan is not valid or scan does not have enough points -> calculate predictions
 *
 *
 *
 * Invarianten erstellen:
 * if scan is not valid or scan does not have enough points
 *
 *
 */




//export typedef Point;
typedef pcl::PointCloud<Point> PointCloud;
//typedef std::map<int, std::pair<Point, Point> > PeopleMap;

const int cluster_size = 2;
// double mahalanobis_dist_gate = 1.6448536269514722;
double mahalanobis_dist_gate = 1.2;
double max_cost = 9999999.;

class LegDetector
{
private:
  ros::Subscriber sub;
  ros::Subscriber local_map_sub;
  laser_geometry::LaserProjection projector_;
  ros::Publisher sensor_msgs_point_cloud_publisher;
  ros::Publisher pcl_cloud_publisher;
  ros::Publisher pos_vel_acc_lleg_pub;
  ros::Publisher pos_vel_acc_rleg_pub;
  ros::Publisher legs_and_vel_direction_publisher;
  ros::Publisher tracking_area_pub;
  ros::Publisher people_pub;
  
  ros::Publisher marker_pub;
  ros::Publisher cov_marker_pub;
  

  std::string transform_link;
  std::string scan_topic;
  std::string local_map_topic;
  
  double x_lower_limit;
  double x_upper_limit;
  double y_lower_limit;
  double y_upper_limit;
  tf2_ros::Buffer tfBuffer;
  tf2_ros::TransformListener tfListener;
  ros::Publisher vis_pub;
  double ransac_dist_threshold;
  std::string circle_fitting;
  double leg_radius;
  std::vector<double> centerOfLegLastMeasurement;
  Point person_center;
//   cv::Point2f left_leg_prediction;
//   cv::Point2f right_leg_prediction;
  int legs_gathered;
  int min_observations;
  // int min_predictions;
  double max_dist_btw_legs;
  int id_counter;
  double z_coordinate;
  double vel_stance_threshold;
  double vel_swing_threshold;
  int state_dimensions;
  int minClusterSize;
  int maxClusterSize;
  double clusterTolerance;
  int occluded_dead_age;
  double variance_observation;

  bool isOnePersonToTrack;

  int legs_marker_next_id;
  int people_marker_next_id;
  int next_leg_id;
  int cov_ellipse_id;

//   double max_nn_gating_distance;
  double max_cov;
  double min_dist_travelled;
  double in_free_space_threshold;
  
  nav_msgs::OccupancyGrid local_map;
  bool got_map;

  //mht
//   std::list<Leg> legs;
  std::vector<Leg> legs;


  //PeopleMap persons;Map map(info.width, info.height);
  std::vector<Leg> removed_legs;
  std::vector<Person> persons;
//   std::vector<std::pair<Leg, Leg> > persons;

  std::vector<Leg> temporary_legs;

//   iirob_filters::KalmanFilterParameters params_;

  // Construct the filter
  iirob_filters::MultiChannelKalmanFilter<double>* left_leg_filter;
  iirob_filters::MultiChannelKalmanFilter<double>* right_leg_filter;

public:
  ros::NodeHandle nh_;
  LegDetector(ros::NodeHandle nh) : nh_(nh), /*params_{std::string(nh_.getNamespace() + "/KalmanFilter")}, */
      tfListener(tfBuffer)
  {
    init();
//     left_leg_filter = new iirob_filters::MultiChannelKalmanFilter<double>();
//     if (!left_leg_filter->configure()) { ROS_ERROR("Configure of filter has failed!"); nh_.shutdown(); }
//     right_leg_filter = new iirob_filters::MultiChannelKalmanFilter<double>();
//     if (!right_leg_filter->configure()) { ROS_ERROR("Configure of filter has failed!"); nh_.shutdown(); }

  }
  ~LegDetector() {}

  void init()
  {
    std::srand(1);

//     nh_.param("scan_topic", scan_topic, std::string("/base_laser_rear/scan"));
    nh_.param("scan_topic", scan_topic, std::string("/scan_unified"));
    nh_.param("transform_link", transform_link, std::string("base_link"));
    nh_.param("local_map_topic", local_map_topic, std::string("/move_base/local_costmap/costmap"));
    nh_.param("x_lower_limit", x_lower_limit, 0.0);
    nh_.param("x_upper_limit", x_upper_limit, 0.5);
    nh_.param("y_lower_limit", y_lower_limit, -0.5);
    nh_.param("y_upper_limit", y_upper_limit, 0.5);
    nh_.param("leg_radius", leg_radius, 0.1);
    nh_.param("min_observations", min_observations, 4);
    // nh_.param("min_predictions", min_predictions, 7);
    nh_.param("max_dist_btw_legs", max_dist_btw_legs, 0.8);
    nh_.param("z_coordinate", z_coordinate, 0.178);
    nh_.param("vel_stance_threshold", vel_stance_threshold, 0.47);
    nh_.param("vel_swing_threshold", vel_swing_threshold, 0.93);
    nh_.param("state_dimensions", state_dimensions, 6);
    nh_.param("minClusterSize", minClusterSize, 3);
    nh_.param("maxClusterSize", maxClusterSize, 100);
    nh_.param("clusterTolerance", clusterTolerance, 0.07);
    nh_.param("isOnePersonToTrack", isOnePersonToTrack, false);
//     nh_.param("max_nn_gating_distance", max_nn_gating_distance, 1.0);
    nh_.param("occluded_dead_age", occluded_dead_age, 10);
    nh_.param("variance_observation", variance_observation, 0.25);
    nh_.param("min_dist_travelled", min_dist_travelled, 0.25);
    nh_.param("max_cov", max_cov, 0.81);
    nh_.param("in_free_space_threshold", in_free_space_threshold, 0.06);

    legs_gathered = id_counter = legs_marker_next_id = next_leg_id = people_marker_next_id = 
	cov_ellipse_id = 0;
    got_map = false;	

    sub = nh_.subscribe<sensor_msgs::LaserScan>(scan_topic, 1, &LegDetector::processLaserScan, this);
    local_map_sub = nh_.subscribe<nav_msgs::OccupancyGrid>(local_map_topic, 10, &LegDetector::localMapCallback, this);
    sensor_msgs_point_cloud_publisher = nh_.advertise<sensor_msgs::PointCloud2> ("scan2cloud", 300);
    pcl_cloud_publisher = nh_.advertise<PointCloud> ("scan2pclCloud", 300);
    vis_pub = nh_.advertise<visualization_msgs::Marker>("leg_circles", 300);
    pos_vel_acc_lleg_pub = nh_.advertise<std_msgs::Float64MultiArray>("pos_vel_acc_lleg", 300);
    pos_vel_acc_rleg_pub = nh_.advertise<std_msgs::Float64MultiArray>("pos_vel_acc_rleg", 300);
    legs_and_vel_direction_publisher = nh_.advertise<visualization_msgs::MarkerArray>("legs_and_vel_direction", 300);
    tracking_area_pub = nh_.advertise<visualization_msgs::Marker>("tracking_area", 300);
    people_pub = nh_.advertise<visualization_msgs::MarkerArray>("people", 300);
    marker_pub = nh_.advertise<visualization_msgs::Marker>("line_strip", 10);
    cov_marker_pub = nh_.advertise<visualization_msgs::MarkerArray>("cov_ellipses", 10);
//
  }
  
  void localMapCallback(const nav_msgs::OccupancyGrid::ConstPtr& msg) 
  {
//     std_msgs::Header header = msg->header;
//     nav_msgs::MapMetaData info = msg->info;
// //     ROS_INFO("Got map %d %d", info.width, info.height);
//     nav_msgs::Map map(info.width, info.height);
//     for (unsigned int x = 0; x < info.width; x++)
//       for (unsigned int y = 0; y < info.height; y++)
// 	map.Insert(Cell(x,y,info.width,msg->data[x+ info.width * y]));
//     local_map = map.Grid();
//     local_map.header = header;
//     local_map.info = info;
//     if (msg)
//     {
//       local_map.info = msg->info;
//       local_map.data = msg->data;
//       local_map.header = msg->header;
//       if(!got_map) { got_map = true; }
//       ROS_WARN("got map");
//     }
    local_map = *msg;
    if(!got_map) { got_map = true;/* printGridToFile();*/ }
    ROS_WARN("got map %s", local_map.header.frame_id.c_str());
  }
  
  void printGridToFile() {
    std::vector<std::vector<bool> > grid;
    int rows = local_map.info.height;
    int cols = local_map.info.width;
    ROS_WARN("rows: %d, cols: %d", rows, cols);
    grid.resize(rows);
    for (int i = 0; i < rows; i++) {
        grid[i].resize(cols);
    }
    int currCell = 0;
    for (int i = 0; i < rows; i++)  {
        for (int j = 0; j < cols; j++)      {
            if (local_map.data[currCell] == 0) // unoccupied cell
                grid[i][j] = false;
            else
                grid[i][j] = true; // occupied (100) or unknown cell (-1)
            currCell++;
        }
    }
    std::ofstream gridFile;
    const char *path="/home/azanov/grid.txt";
    gridFile.open(path);
    for (int i = grid.size() - 1; i >= 0; i--) {        
        for (int j = 0; j < grid[0].size(); j++) {
        gridFile << (grid[i][j] ? "1" : "0");           
        }
        gridFile << std::endl;
    }
    gridFile.close();
  }

  double getRandomNumberFrom0To1()
  {
      return (double)rand() / (double)RAND_MAX ;
  }

  void handleNotSetParameter(std::string parameter)
  {
      ROS_ERROR("Parameter %s not set, shutting down node...", parameter.c_str());
      nh_.shutdown();
  }

  double calculateNorm(Point p)
  {
    return std::sqrt(std::pow(p.x, 2) + std::pow(p.y, 2));
  }

  int calculateGaitPhase(Point pos_left, Point pos_right, Point vel_left, Point vel_right)
  {
    double velocity_left = calculateNorm(vel_left);
    double velocity_right = calculateNorm(vel_right);

    bool isRightInStance, isLeftInStance, isRightInSwingPhase, isLeftInSwingPhase;

    if (velocity_right < velocity_left || velocity_right < vel_stance_threshold) {
      isRightInStance = true;
    }
    if (velocity_right > velocity_left || velocity_right > vel_swing_threshold) {
      isRightInSwingPhase = true;
    }
    if (velocity_left < velocity_right || velocity_left < vel_stance_threshold) {
      isRightInStance = true;
    }
    if (velocity_left > velocity_right || velocity_left > vel_swing_threshold) {
      isRightInSwingPhase = true;
    }
    // phase 0
    if (isRightInStance && isLeftInStance) { return 0; }

    double inner_pos_vel_product = (pos_left.x - pos_right.x) * vel_left.x +
				   (pos_left.y - pos_right.y) * vel_left.y;
    // phase 1
    if (isLeftInSwingPhase && isRightInStance && inner_pos_vel_product > 0) { return 1; }
    // phase 2
    if (isLeftInSwingPhase && isRightInStance && inner_pos_vel_product <= 0) { return 2; }

    inner_pos_vel_product = (pos_right.x - pos_left.x) * vel_right.x +
			    (pos_right.y - pos_left.y) * vel_right.y;
    // phase 3
    if (isRightInSwingPhase && isLeftInStance && inner_pos_vel_product > 0) { return 3; }
    // phase 4
    if (isRightInSwingPhase && isLeftInStance && inner_pos_vel_product <= 0) { return 4; }

    // phase 5
    return 5;
  }

  void considerGaitPhase() {

        /*

        unlikely:

        Phase 0 to Phase 5,
        Phase 1 to Phases 0, 3, 4 and 5,
        Phase 2 to Phases 1, 4 and 5,
        Phase 3 to Phases 0, 1, 2 and 5,
        Phase 4 to Phases 2, 3 and 5.

        */
  }


//   typedef typename Filter::MeasurementSpaceVector MeasurementSpaceVector;
//   typedef std::vector<MeasurementSpaceVector> Measurements;
//   void GNN(PointCloud measurements)
//   {
// //     template<size_t StateDim, size_t MeasurementDim>
//
// //     typedef KalmanFilter<StateDim, MeasurementDim> Filter;
//
// //     typedef std::vector<Filter> Filters;
//
//     const size_t m = measurements.points.size();
//     const size_t f = legs.size();
//
//     // create matrix for calculating distances between measurements and predictions
//     // additional rows for initializing filters (weightet by 1 / (640 * 480))
//     Eigen::MatrixXd w_ij(m, f + m);
//
//     w_ij = Eigen::MatrixXd::Zero(m, f + m);
//
//     // get likelihoods of measurements within track pdfs
//     for ( size_t i = 0; i < m; ++i )
//     {
// 	for ( size_t j = 0; j < f; ++j )
// 	{
// 	  double temp = legs[j].likelihood(measurements.points[i].x, measurements.points[i].y);
// 	  //if (!legs[j].likelihood(measurements.points[i].x, measurements.points[i].y, temp)) { continue; }
// 	  w_ij(i, j) = temp;
// 	}
//     }
//
//     // TODO: must changed to generic
//     // weights for initializing new filters
//     for ( size_t j = f; j < m + f; ++j )
// 	w_ij(j - f, j) = 1. / ((x_upper_limit - x_lower_limit) * (y_upper_limit - y_lower_limit));
//
//     // solve the maximum-sum-of-weights problem (i.e. assignment problem)
//     // in this case it is global nearest neighbour by minimizing the distances
//     // over all measurement-filter-associations
//     Auction<double>::Edges assignments = Auction<double>::solve(w_ij);
//
//     std::vector<Leg> newLegs;
//
//     // for all found assignments
//     for ( const auto & e : assignments )
//     {
// 	// is assignment an assignment from an already existing filter to a measurement?
// 	if ( e.y < f )
// 	{
// 	    // update filter and keep it
// 	    updateLeg(legs[e.y], measurements.points[e.x]);
// 	    newLegs.emplace_back(legs[e.y]);
// 	}
// 	else // is this assignment a measurement that is considered new?
// 	{
// 	    // create filter with measurment and keep it
// 	    Leg l;
// 	    if (!l.configure(state_dimensions, min_predictions, min_observations, measurements.points[e.x])) { ROS_ERROR("GNN: Configuring of leg failed!"); continue; }
// 	    newLegs.emplace_back(l);
// 	}
//     }
//
//     // current filters are now the kept filters
//     legs = newLegs;
//   }


  visualization_msgs::Marker getCovarianceEllipse(int id, const double meanX, const double meanY, const Eigen::MatrixXd& S)
  {

    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(S);
    double l1 = solver.eigenvalues().x();
    double l2 = solver.eigenvalues().y();
    Eigen::VectorXd e1 = solver.eigenvectors().col(0);
    Eigen::VectorXd e2 = solver.eigenvectors().col(1);

    double scale95 = std::sqrt(5.991);
    double R1 = scale95 * std::sqrt(l1);
    double R2 = scale95 * std::sqrt(l2);
    double tilt = std::atan2(e2.y(), e2.x());


    Eigen::Quaterniond q;
    q = Eigen::AngleAxisd(tilt, Eigen::Vector3d::UnitZ());
    double orientation_w = q.w();
    double orientation_x = q.x();
    double orientation_y = q.y();
    double orientation_z = q.z();

//     ROS_INFO("meanX: %f, meanY: %f, scale: %f, R1: %f, R2: %f, angle: %f",
// 	     meanX, meanY, scale95, R1, R2, tilt);
    
    return getOvalMarker(id, meanX, meanY, orientation_x, orientation_y, orientation_z,
      orientation_w, R1, R2, 0.0, 0.0, 1.0);
  }



  bool laserScanToPointCloud2(const sensor_msgs::LaserScan::ConstPtr& scan, sensor_msgs::PointCloud2& cloud)
  {
    if (!scan) { ROS_DEBUG("Laser scan pointer was not set!"); return false; }
    projector_.projectLaser(*scan, cloud);
    return true;
  }

  bool tfTransformOfPointCloud2(const sensor_msgs::LaserScan::ConstPtr& scan,
				sensor_msgs::PointCloud2& from, sensor_msgs::PointCloud2& to)
  {
    geometry_msgs::TransformStamped transformStamped;
    try{
      transformStamped = tfBuffer.lookupTransform(transform_link, scan->header.frame_id, ros::Time(0));
    }
    catch (tf2::TransformException &ex) {
      ROS_WARN("%s",ex.what());
      ros::Duration(1.0).sleep();
      return false;
    }
    tf2::doTransform(from, to, transformStamped);
    return true;
  }

  bool filterPCLPointCloud(const PointCloud& in, PointCloud& out)
  {
    if (in.points.size() < 5)
    {
      ROS_DEBUG("Filtering: Too small number of points in the input PointCloud!");
      return false;
    }

//     PointCloud::Ptr pass_through_filtered_x (new PointCloud());
//     PointCloud::Ptr pass_through_filtered_y (new PointCloud());
//     PointCloud::Ptr sor_filtered (new PointCloud());
//
    PointCloud pass_through_filtered_x;
    PointCloud pass_through_filtered_y;
//     PointCloud sor_filtered;

    pcl::PassThrough<Point> pass;
    pass.setInputCloud(in.makeShared());
    pass.setFilterFieldName("x");
    pass.setFilterLimits(x_lower_limit, x_upper_limit);
    pass.setFilterLimitsNegative (false);
    pass.filter ( pass_through_filtered_x );
    if ( pass_through_filtered_x.points.size() < 5)
    {
      ROS_DEBUG("Filtering: Too small number of points in the PointCloud after PassThrough filter in x direction!");
      return false;
    }
    pass.setInputCloud( pass_through_filtered_x.makeShared());
    pass.setFilterFieldName("y");
    pass.setFilterLimits(y_lower_limit, y_upper_limit);
    pass.filter (pass_through_filtered_y);
    if ( pass_through_filtered_y.points.size() < 5)
    {
      ROS_DEBUG("Filtering: Too small number of points in the PointCloud after PassThrough filter in y direction!");
      return false;
    }

    pcl::RadiusOutlierRemoval<Point> outrem;



    // build the filter
    outrem.setInputCloud(pass_through_filtered_y.makeShared());
    outrem.setRadiusSearch(clusterTolerance);
    outrem.setMinNeighborsInRadius (minClusterSize);
    // apply filter
    outrem.filter (out);

    if (out.points.size() < 5)
    {
      ROS_DEBUG("Filtering: Too small number of points in the resulting PointCloud!");
      return false;
    }
    return true;
  }

  Leg initLeg(const Point& p)
  {
    Leg l(getNextLegId(), p, occluded_dead_age,
      variance_observation, min_observations, state_dimensions, min_dist_travelled);
    return l;
  }

  // void updateLeg(Leg& l, const Point& p)
  // {
  //   ROS_INFO("updateLeg");
  //   printLegsInfo();
  //   std::vector<double> in, out;
  //   in.push_back(p.x); in.push_back(p.y);
  //   l.update(in, out);
  //   ROS_INFO("AFTER updateLeg");
  //   printLegsInfo();
  // }

  void removeLeg(int index)
  {
    ROS_INFO("removeLeg  legs[index].hasPair: %d, legs[i].getPeopleId: %d", legs[index].hasPair(), legs[index].getPeopleId());
//     printLegsInfo();
    if (legs[index].hasPair())
    {
      int id = legs[index].getPeopleId();
      for (int i = 0; i < legs.size(); i++)
      {
	      if (i != index && legs[i].getPeopleId() == id)
	      {
		      legs[i].setHasPair(false);
	      }
      }
    }
    if (legs[index].getPeopleId() != -1)
	{
		//legs[index].setPeopleId(-1);
		removed_legs.push_back(legs[index]);
	}
    legs.erase(legs.begin() + index);
    ROS_INFO("AFTER removeLeg");
//     printLegsInfo();
  }



/*
  void predictLeg(int i)
  {
    ROS_INFO("PredictLeg  legs[i].getPredictions: %d, legs[i].getPeopleId: %d", legs[i].getPredictions(), legs[i].getPeopleId());
    printLegsInfo();
    if (legs[i].getPredictions() >= min_predictions)
    {
      removeLeg(i);
    }
    else
    {
      legs[i].predict();
    }
    ROS_INFO("AFTER predictLeg");
    printLegsInfo();
  }*/


  void printLegsInfo(std::vector<Leg> vec, std::string name)
  {
    ROS_INFO("\n\n%s\n\n", name.c_str());
    for (Leg& l : vec)
    {
      ROS_INFO("legId: %d, peopleId: %d, pos: (%f, %f), observations: %d, hasPair: %d, missed: %d, dist_traveled: %f",
      l.getLegId(), l.getPeopleId(), l.getPos().x, l.getPos().y, l.getObservations(), l.hasPair(), l.getOccludedAge(), l.getDistanceTravelled());
    }
  }

  void visLegs(PointCloud& cloud)
  {
    cloud.points.clear();
    visualization_msgs::MarkerArray ma_leg_pos;
    visualization_msgs::MarkerArray ma_leg_vel;
    int max_id = 0;
    int id = 0;
    for (Leg& l : legs)
    {
//       ROS_INFO("VISlegs peopleId: %d, pos: (%f, %f), predictions: %d, observations: %d, hasPair: %d",
//       l.getPeopleId(), l.getPos().x, l.getPos().y, l.getPredictions(), l.getObservations(), l.hasPair());
      cloud.points.push_back(l.getPos());
      ma_leg_pos.markers.push_back(getMarker(l.getPos().x, l.getPos().y, z_coordinate, max_id/*, true*/));
//       ma_leg_vel.markers.push_back(getArrowMarker(l.getPos().x + 0.01, l.getPos().y + 0.01,
// 	  l.getPos().x + l.getVel().x + 0.01, l.getPos().y + l.getVel().y + 0.01, max_id));
      max_id++;
      id++;
    }
    legs_and_vel_direction_publisher.publish(ma_leg_pos);
//     marker_array_vel_publisher.publish(ma_leg_vel);
//     pcl_cloud_publisher.publish(cloud.makeShared());
  }

  void removeOldMarkers(int nextId, ros::Publisher ma_publisher)
  {
    visualization_msgs::MarkerArray ma;
    for (int i = 0; i < nextId; i++) {
      visualization_msgs::Marker marker;
      marker.header.frame_id = transform_link;
      marker.header.stamp = ros::Time();
      marker.ns = nh_.getNamespace();
      marker.id = i;
      marker.action = visualization_msgs::Marker::DELETE;
      ma.markers.push_back(marker);
    }
    ma_publisher.publish(ma);
  }

  void visLegs()
  {
    if (legs_marker_next_id != 0) {
      removeOldMarkers(legs_marker_next_id, legs_and_vel_direction_publisher);
      legs_marker_next_id = 0;
    }
    visualization_msgs::MarkerArray ma_leg;
    for (Leg& l : legs)
    {
//       if (l.getDistanceTravelled() < min_dist_travelled) { continue; }
//       ROS_INFO("VISlegs peopleId: %d, pos: (%f, %f), observations: %d, hasPair: %d",
//       l.getPeopleId(), l.getPos().x, l.getPos().y, l.getObservations(), l.hasPair());

//       ma_leg.markers.push_back(getMarker(l.getPos().x, l.getPos().y, getNextLegsMarkerId()));
//       if (l.getObservations() == 0 || calculateNorm(l.getVel()) < 0.01) { continue; }
      
//       double pos_x = l.getPos().x + std::min(l.getPos().x * 0.1, 0.3 * leg_radius);
//       double pos_y = l.getPos().y + std::min(l.getPos().y * 0.1, 0.5 * leg_radius);
      double pos_x = l.getPos().x;
      double pos_y = l.getPos().y;
      ma_leg.markers.push_back(getArrowMarker(pos_x, pos_y,
	       pos_x + 0.5 * l.getVel().x, pos_y + 0.5 * l.getVel().y, getNextLegsMarkerId()));
    }
    legs_and_vel_direction_publisher.publish(ma_leg);
    
//     if (legs.size() != 2) {
//       return;
//     }
//     if (legs[0].getHistory().size() < 1 && legs[1].getHistory().size() < 1) { return; }
//     bool isLeft = legs[0].getPos().x < legs[1].getPos().x;
//     pub_leg_posvelacc(legs[0].getHistory().back(), isLeft);
//     pub_leg_posvelacc(legs[1].getHistory().back(), !isLeft); 
  }

  int getNextLegsMarkerId()
  {
    return legs_marker_next_id++;
  }


  visualization_msgs::Marker getArrowMarker(double start_x, double start_y, double end_x, double end_y, int id)
  {
    visualization_msgs::Marker marker;
    marker.header.frame_id = transform_link;
    marker.header.stamp = ros::Time();
    marker.ns = nh_.getNamespace();
    marker.id = id;
    marker.type = visualization_msgs::Marker::ARROW;
    marker.action = visualization_msgs::Marker::ADD;
//     marker.pose.position.x = x;
//     marker.pose.position.y = y;
//     marker.pose.position.z = z_coordinate / 2;
//     marker.pose.orientation.x = 0.0;
//     marker.pose.orientation.y = 0.0;
//     marker.pose.orientation.z = 0.0;
//     marker.pose.orientation.w = 1.0;

    geometry_msgs::Point start, end;
    start.x = start_x;
    start.y = start_y;
    start.z = z_coordinate / 2;
    end.x = end_x;
    end.y = end_y;
    end.z = z_coordinate / 2;
    marker.pose.position.x = 0.;
    marker.pose.position.y = 0.;
    marker.pose.position.z = 0.1;
    marker.points.push_back(start);
    marker.points.push_back(end);
    marker.scale.x = 0.05;
    marker.scale.y = 0.1;
    marker.scale.z = 0.2;
    marker.color.a = 1.0; // Don't forget to set the alpha!
    marker.color.r = 1.0;
//     if (id == 2)
//       marker.color.g = 1.0;
//     if (id == 1)
//       marker.color.b = 1.0;
    //only if using a MESH_RESOURCE marker type:
//     marker.mesh_resource = "package://pr2_description/meshes/base_v0/base.dae";
    return marker;
  }

  visualization_msgs::Marker getMarker(double x, double y, double scale_z, int id/*, bool isMeasurement*/)
  {
    visualization_msgs::Marker marker;
    marker.header.frame_id = transform_link;
    marker.header.stamp = ros::Time();
    marker.ns = nh_.getNamespace();
    marker.id = id;
    marker.type = visualization_msgs::Marker::CYLINDER;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.x = x;
    marker.pose.position.y = y;
    marker.pose.position.z = z_coordinate / 2;
//     if (!isMeasurement) { marker.pose.position.z += z_coordinate; }
//     marker.pose.orientation.x = 0.0;
//     marker.pose.orientation.y = 0.0;
//     marker.pose.orientation.z = 0.0;
//     marker.pose.orientation.w = 1.0;
    marker.scale.x = leg_radius;
    marker.scale.y = leg_radius;
    marker.scale.z = scale_z;
    marker.color.a = 1.0; // Don't forget to set the alpha!
//     if (isMeasurement)
      marker.color.r = 1.0;
//     if (id == 2)
//     if (!isMeasurement)
//       marker.color.g = 1.0;
//     if (id == 1)
//       marker.color.b = 1.0;
    //only if using a MESH_RESOURCE marker type:
//     marker.mesh_resource = "package://pr2_description/meshes/base_v0/base.dae";
    return marker;
  }

  void printClusterInfo(const PointCloud& cluster_centroids)
  {
//     ROS_INFO("Clusters:");
//     for (int i = 0; i < cluster_centroids.points.size(); i++)
//     {
//       ROS_INFO("cluster %d: (%f, %f)", i, cluster_centroids.points[i].x, cluster_centroids.points[i].y);
//     }
  }

  void matchLegCandidates(PointCloud cluster_centroids)
  {
    // printClusterInfo(cluster_centroids);
    //PointCloud predictions;
    //predictions.header = cluster_centroids.header;
    //computeKalmanFilterPredictions(predictions);
    //ROS_INFO("predictions: %d, cluster_centroids: %d", (int) predictions.points.size(), (int) cluster_centroids.points.size());
    //if (predictions.points.size() == 0 && cluster_centroids.points.size() == 0) { ROS_INFO("There are no leg candidates and no predictions!"); return; }


    // there are no measurements for legs
    // if (cluster_centroids.points.size() == 0)
    // {
    //   ROS_INFO("There are only predictions!");
    //   for (int i = 0; i < legs.size(); i++)
    //   {
    //      predictLeg(i);
    //   }
    //   return;
    // }

    //// there are no observed legs
//     if (predictions.points.size() == 0)
//     {
//       ROS_INFO("There are only new leg candidates!");
//       for (Point p : cluster_centroids.points)
//       {
// 	initLeg(p);
//       }
//       return;
//     }

//     visualization_msgs::MarkerArray ma;


    for (int i = 0; i < legs.size(); i++)
    {
      legs[i].predict();
    }


    // if there is matched measurement then update else predict
    for (int i = 0; i < legs.size(); i++)
    {
      if (cluster_centroids.points.size() == 0) { legs[i].missed();  continue; }

      Point match;
      //ma.markers.push_back(getMarker(prediction.x, prediction.y, 10, false));

//       if (!findAndEraseMatch(legs[i].getPos(), cluster_centroids, match, max_nn_gating_distance)) { legs[i].missed(); }
      if (!findAndEraseMatchWithCov(i, cluster_centroids, match)) { legs[i].missed(); }
      else { legs[i].update(match); }

      // Eigen::MatrixXd gate;
      // if (!legs[i].getGatingMatrix(gate)) { ROS_WARN("Could not get the gating matrix!"); predictLeg(i); continue; }
      //
      // pubCovarianceAsEllipse(prediction.x, prediction.y, gate);

      // if (!findAndEraseMatchWithCov(i, prediction, cluster_centroids, match)) { predictLeg(i); }
      // else { updateLeg(legs[i], match); }
    }
    cullDeadTracks(legs);

    if (legs.size() < 2) {
      for (Point& p : cluster_centroids.points)
      {
	legs.push_back(initLeg(p));
      }
    }
  }

      /*
=======
//     for (int i = 0; i < legs.size(); i++)
//     {
//       if (cluster_centroids.points.size() == 0) { predictLeg(i); continue; }
//

      // NN
//       Point prediction, nearest;
//       prediction = legs[i].computePrediction();
//       ma.markers.push_back(getMarker(prediction.x, prediction.y, 10, false));
//       double radius = (2 + legs[i].getPredictions()) * leg_radius;
//       if (!findAndEraseMatch(prediction, cluster_centroids, nearest, radius)) { predictLeg(i); }
//       else { updateLeg(legs[i], nearest); }


>>>>>>> origin/master
      // GNN
      const size_t m = cluster_centroids.points.size();
      const size_t f = legs.size();

      // create matrix for calculating distances between measurements and predictions
      // additional rows for initializing filters (weightet by 1 / (640 * 480))
      Eigen::MatrixXd w_ij(m, f + m);

      w_ij = Eigen::MatrixXd::Zero(m, f + m);

      // get likelihoods of measurements within track pdfs
      for ( size_t i = 0; i < m; ++i )
      {
	  for ( size_t j = 0; j < f; ++j )
	  {
	    double temp;
	    if (!legs[j].likelihood(cluster_centroids.points[i].x, cluster_centroids.points[i].y, temp)) { continue; }
	    w_ij(i, j) = temp;
	  }
      }

      // TODO: must changed to generic
      // weights for initializing new filters
      for ( size_t j = f; j < m + f; ++j )
	  w_ij(j - f, j) = 1. / ((x_upper_limit - x_lower_limit) * (y_upper_limit - y_lower_limit));

      // solve the maximum-sum-of-weights problem (i.e. assignment problem)
      // in this case it is global nearest neighbour by minimizing the distances
      // over all measurement-filter-associations
      Auction<double>::Edges assignments = Auction<double>::solve(w_ij);


      std::vector<Leg> newLegs;

      for ( const auto & e : assignments )
      {
	  // is assignment an assignment from an already existing filter to a measurement?
	  if ( e.y < f )
	  {
	      // update filter and keep it
	      updateLeg(legs[e.y], cluster_centroids.points[e.x]);
	      newLegs.emplace_back(legs[e.y]);
	  }
	  else // is this assignment a measurement that is considered new?
	  {
	      // create filter with measurment and keep it
	      Leg l(state_dimensions, min_predictions, min_observations);
	      if (!l.configure(cluster_centroids.points[e.x])) { ROS_ERROR("GNN: Configuring of leg failed!"); continue; }
	      newLegs.emplace_back(l);
	  }
      }

      legs = newLegs;
	*/
//     }
//     legs_and_vel_direction_publisher.publish(ma);

    // if there is enough measurements for legs try to find people

    // findPeople();

    // save new measurements of legs
  //   for (Point p : cluster_centroids.points)
  //   {
  //     initLeg(p);
  //   }
  // }
  
  double how_much_in_free_space(double x, double y)
  {
    // Determine the degree to which the position (x,y) is in freespace according to our local map
//     if local_map == None
//       return self.in_free_space_threshold*2
    if (!got_map) {
      return in_free_space_threshold * 2;
    }
    int map_x = int(std::round((x - local_map.info.origin.position.x)/local_map.info.resolution));
    int map_y = int(std::round((y - local_map.info.origin.position.y)/local_map.info.resolution));
    
    double sum = 0;
    int kernel_size = 2;
    for (int i = map_x - kernel_size; i <= map_x + kernel_size; i++) {  
      for (int j = map_y - kernel_size; j <= map_y + kernel_size; j++) { 
	  if (i + j * local_map.info.height < local_map.info.height * local_map.info.width) {
	      sum += local_map.data[i + j * local_map.info.height];
	  } else {  
	      // We went off the map! position must be really close to an edge of local_map
	      return in_free_space_threshold * 2;
	  }
      }
    }
    double percent = sum / (( std::pow((2. * kernel_size + 1.), 2)) * 100.);
    return percent;
  }



  bool findAndEraseMatchWithCov(int legIndex, PointCloud& cloud, Point& out)
  {
    if (cloud.points.size() == 0) { ROS_INFO("findAndEraseMatchWithCov: Cloud is emty!"); return false; }



    int index = -1;
    double min_dist = max_cost;
    for (int i = 0; i < cloud.points.size(); i++)
    {
      double cov = legs[legIndex].getMeasToTrackMatchingCov();
      double mahalanobis_dist = std::sqrt((std::pow((cloud.points[i].x - legs[legIndex].getPos().x), 2) +
	  std::pow((cloud.points[i].y - legs[legIndex].getPos().y), 2)) / cov);
      if (mahalanobis_dist < min_dist)
      {
	index = i;
	min_dist = mahalanobis_dist;
      }
    }

    if (index == -1) { ROS_INFO("findAndEraseMatchWithCov: index = -1!"); return false; }


    out = cloud.points[index];
    cloud.points.erase(cloud.points.begin() + index);

    return true;
  }

  void separateLegs(int i, int j)
  {
    legs[i].setHasPair(false);
    legs[j].setHasPair(false);
    legs[i].setPeopleId(-1);
    legs[j].setPeopleId(-1);
  }

  void checkDistanceOfLegs()
  {
    for (int i = 0; i < legs.size(); i++)
    {
      if (!legs[i].hasPair()) { continue; }
      for (int j = i + 1; j < legs.size(); j++)
      {
	if (legs[i].getPeopleId() == legs[j].getPeopleId()) {
	  if (distanceBtwTwoPoints(legs[i].getPos(), legs[j].getPos()) > max_dist_btw_legs)
	  {
// 	    ROS_INFO("separate leg %d and %d", i, j);
	    separateLegs(i, j);
	  }
	  break;
	}
      }
    }
  }

  Person initPerson(const Point& pos, int id)
  {
    Person p(id, pos, occluded_dead_age,
      variance_observation, min_observations);
    return p;
  }

  void findPeople(PointCloud& new_people, std::vector<int>& new_people_idx)
  {
    checkDistanceOfLegs();
    for (int i = 0; i < legs.size(); i++)
    {
      if (legs[i].hasPair() || /*legs[i].getDistanceTravelled() < min_dist_travelled ||*/
	(legs[i].getPeopleId() == -1 && legs[i].getObservations() < min_observations)) 
	{ continue; }
//       if ((legs[i].getPeopleId() != -1 || legs[i].getObservations() >= min_observations) && !)
//       {
      findSecondLeg(i, new_people, new_people_idx);
//       }
    }
  }

  void findSecondLeg(int fst_leg, PointCloud& new_people, std::vector<int>& new_people_idx)
  {
//     ROS_INFO("findSecondLeg");
    //PointCloud potential_legs;
    std::vector<int> indices_of_potential_legs;
    for (int i = fst_leg + 1; i < legs.size(); i++)
    {
      if (/*i == fst_leg || */legs[i].hasPair() || legs[i].getObservations() < min_observations
	|| distanceBtwTwoPoints(legs[fst_leg].getPos(), legs[i].getPos()) > max_dist_btw_legs
	|| distanceBtwTwoPoints(legs[fst_leg].getPos(), legs[i].getPos()) < leg_radius)
      {
	      continue;
      }

//       potential_legs.points.push_back(legs[i].getPos());
      indices_of_potential_legs.push_back(i);
    }

    if (indices_of_potential_legs.size() == 0) { ROS_DEBUG("There is no potential second leg!"); return; }

    if (indices_of_potential_legs.size() == 1) {
      setPeopleId(fst_leg, indices_of_potential_legs[0], new_people, new_people_idx);
      return;
    }
    int snd_leg = -1;
  	//snd_leg = findMatch(legs[fst_leg].getPos(), potential_legs, indices);

    double max_gain = 0.;
    for (int i = 0; i < indices_of_potential_legs.size(); i++)
    {
      double gain = 0.;
      bool isHistoryDistanceValid = true;
//       std::vector<std::vector<double> >::iterator fst_history_it = legs[fst_leg].getHistory().begin(),
//         snd_history_it = legs[indices_of_potential_legs[i]].getHistory().begin();
      int history_size = legs[fst_leg].getHistory().size();
      if (history_size != min_observations || 
	legs[indices_of_potential_legs[i]].getHistory().size() != history_size)
      {
        ROS_WARN("History check: vectors are not equal in size!");
        return;
      }
      for (int j = 0; j < history_size - 1; j++)
      {
// 	std::vector<double>& fst_history = *fst_history_it;
// 	std::vector<double>& snd_history = *snd_history_it;
	
// 	std::vector<double>::iterator fst_history = legs[fst_leg].getHistory()[j].begin();
// 	std::vector<double>::iterator snd_history = legs[indices_of_potential_legs[i]].getHistory()[j];
	
	int fst_history_size = legs[fst_leg].getHistory()[j].size();
	int snd_history_size = legs[indices_of_potential_legs[i]].getHistory()[j].size();
	
// 	ROS_WARN("fst_history: %d, snd_history: %d", fst_history_fst_value, snd_history_size);
	
	if (fst_history_size != state_dimensions || snd_history_size != state_dimensions)
	{
	  ROS_WARN("History check: stae vectors are not valid!");
	  return;
	}
	
// 	std::string s = "";
// 	for (int k = 0; k <= history_size; k++) {
// 	  for (double d : fst_history) {
// 	    s += std::to_string(d); s += " ";
// 	  }
// 	  s += "\n";
// 	}
// 	ROS_WARN("fst_history: \n%s", s.c_str());
// 	s = "";
// 	for (int k = 0; k <= history_size; k++) {
// 	  for (double d : snd_history) {
// 	    s += std::to_string(d); s += " ";
// 	  }
// 	  s += "\n";
// 	}
// 	ROS_WARN("snd_history: \n%s", s.c_str());
	
	double dist = distanceBtwTwoPoints(legs[fst_leg].getHistory()[j][0], 
					   legs[fst_leg].getHistory()[j][1],
	  legs[indices_of_potential_legs[i]].getHistory()[j][0], 
	  legs[indices_of_potential_legs[i]].getHistory()[j][1]);
	
// 	ROS_WARN("dist btw (%f, %f) and (%f, %f): %f, ids: %d %d", legs[fst_leg].getHistory()[j][0], 
// 					   legs[fst_leg].getHistory()[j][1],
// 	  legs[indices_of_potential_legs[i]].getHistory()[j][0], 
// 	  legs[indices_of_potential_legs[i]].getHistory()[j][1],
// 		 dist, legs[fst_leg].getLegId(), legs[indices_of_potential_legs[i]].getLegId());
	
	if (dist > max_dist_btw_legs)
	{
	  ROS_DEBUG("History check: distance is not valid!");
	  isHistoryDistanceValid = false;
	  break;
	}
      
//       	if (distanceBtwTwoPoints(fst_history_it->at(0), fst_history_it->at(1),
//       	  snd_history_it->at(0), snd_history_it->at(1)) > max_dist_btw_legs)
//       	{
//       	  ROS_DEBUG("History check: distance is not valid!");
//       	  isHistoryDistanceValid = false;
//       	  break;
//       	}

//         double dist = distanceBtwTwoPoints(fst_history_it->at(0), fst_history_it->at(1),
//       	  snd_history_it->at(0), snd_history_it->at(1));
        double forgettingFactor = std::pow(0.5, history_size - 1 - j);
        gain += forgettingFactor * (1 - dist / std::sqrt(200));

//       	fst_history_it++; snd_history_it++;

      }

      gain /= history_size;

      // if (isHistoryDistanceValid) { snd_leg = i; break; }
      if (!isHistoryDistanceValid) { continue; }
      if (max_gain < gain) {
        max_gain = gain;
        snd_leg = indices_of_potential_legs[i];
      }
    }

    if (snd_leg == -1) { ROS_INFO("Could not find second leg!"); return; }

    setPeopleId(fst_leg, snd_leg, new_people, new_people_idx);
  }

  double distanceBtwTwoPoints(double x1, double y1, double x2, double y2)
  {
    return std::sqrt(std::pow(x1 - x2, 2) + std::pow(y1 - y2, 2));
  }


  /*int findMatch(const Point& searchPoint, const PointCloud& cloud, const std::vector<int>& indices)
  {
    ROS_INFO("findMatch for (%f, %f) with radius %f", searchPoint.x, searchPoint.y, radius_of_person);
    printCloudPoints(cloud);
    int out_index = -1;
    if (cloud.points.size() == 0) { return out_index; }
    pcl::KdTreeFLANN<Point> kdtree;
    kdtree.setInputCloud (cloud.makeShared());
    std::vector<int> pointIdxRadius;
    std::vector<float> pointsSquaredDistRadius;
    int count = kdtree.radiusSearch (searchPoint, radius_of_person, pointIdxRadius, pointsSquaredDistRadius);
<<<<<<< HEAD

    if (count == 0) { return out_index; }
//     else if (count == 1)
//     {
      int K = 1;
      std::vector<int> pointsIdx(K);
      std::vector<float> pointsSquaredDist(K);
      kdtree.nearestKSearch (searchPoint, K, pointsIdx, pointsSquaredDist);
      ROS_INFO("legs.size: %d, pointsIdx[0]: %d, indices[pointsIdx[0]]: %d, pointsSquaredDist: %f",
	(int) legs.size(), pointsIdx[0], indices[pointsIdx[0]], pointsSquaredDist[0]);
      if (pointsSquaredDist[0] <= radius_of_person && pointsSquaredDist[0] >= 0.5 * leg_radius) {
	out_index = indices[pointsIdx[0]];
      }
//     }
//     else //(count > 1)
//     {
//       ROS_INFO("mahalanobis");
//       // interesting more than one leg matched
//       // use mahalanobis distance and some other data association methods
//
//
//       int index = -1;
//       double max_likelihood = -1.;
//       for (int i = 0; i < cloud.points.size(); i++)
//       {
// 	double likelihood;
// 	if (!legs[legIndex].likelihood(cloud.points[i].x, cloud.points[i].y, likelihood)) { return out_index; }
// 	ROS_INFO("legIndex: %d, sPoint.x: %f, sPoint.y: %f, max_likelihood: %f, cloudPoint[%d].x: %f, cloudPoint[%d].y: %f, likelihood: %f",
// 	  legIndex, searchPoint.x, searchPoint.y, max_likelihood, i, cloud.points[i].x, i, cloud.points[i].y, likelihood);
// 	if (likelihood > max_likelihood)
// 	{
// 	  index = i;
// 	  max_likelihood = likelihood;
// 	}
//       }
//
//       if (index == -1) { return out_index; }
//     }

    return out_index;
  }*/


  void setPeopleId(int fst_leg, int snd_leg, PointCloud& new_people, std::vector<int>& new_people_idx)
  {
	  int id = -1;
	  bool isIdSet = false;
	  isIdSet = legs[fst_leg].getPeopleId() != -1 || legs[snd_leg].getPeopleId() != -1;
	  if (isIdSet && (legs[snd_leg].getPeopleId() ==  -1 || legs[snd_leg].getPeopleId() == legs[fst_leg].getPeopleId()) )
// 	  if (isIdSet && legs[fst_leg].getPeopleId() !=  -1)
	  {
			id = legs[fst_leg].getPeopleId();
			eraseRemovedLeg(id);
	  }
	  else if (isIdSet && legs[fst_leg].getPeopleId() ==  -1)
	  {
			id = legs[snd_leg].getPeopleId();
			eraseRemovedLeg(id);
	  }
	  else if (isIdSet && legs[snd_leg].getPeopleId() != legs[fst_leg].getPeopleId())
	  {
		  // legs from not same people
		  //return;
			id = std::min(legs[fst_leg].getPeopleId(), legs[snd_leg].getPeopleId());

			eraseRemovedLeg(legs[fst_leg].getPeopleId());
			eraseRemovedLeg(legs[snd_leg].getPeopleId());
	  }
	  else
	  {
		  id = id_counter++;
	  }




	  legs[fst_leg].setPeopleId(id);
	  legs[snd_leg].setPeopleId(id);
	  legs[fst_leg].setHasPair(true);
	  legs[snd_leg].setHasPair(true);

// 	  Point person;
// 	  person.x = (legs[fst_leg].getPos().x + legs[snd_leg].getPos().x) / 2;
// 	  person.y = (legs[fst_leg].getPos().y + legs[snd_leg].getPos().y) / 2;

// 	  new_people.points.push_back(person);
// 	  new_people_idx.push_back(id);

	  //std::pair<PeopleMap::iterator, bool> ret;
	  //std::pair <Point, Point> leg_points = std::make_pair(legs[fst_leg].getPos(), legs[fst_leg].getPos());
	  //ret = persons.insert ( std::pair<int, std::pair <Point, Point> >(id, leg_points) );
	  //if (ret.second==false) {
		//std::cout << "element 'z' already existed";
		//std::cout << " with a value of " << ret.first->second << '\n';
	  //}
	  //persons.add(id, std::make_pair(legs[fst_leg].getPos(), legs[fst_leg].getPos()));
  }

  void eraseRemovedLegsWithoutId()
  {
	  for (std::vector<Leg>::iterator it = removed_legs.begin(); it != removed_legs.end(); it++)
	  {
		  if (it->getPeopleId() == -1)
		  {
			  removed_legs.erase(it);
		  }
	  }
  }

  void eraseRemovedLeg(int id)
  {
	  for (std::vector<Leg>::iterator it = removed_legs.begin(); it != removed_legs.end(); it++)
	  {
		  if (it->getPeopleId() == id)
		  {
			  removed_legs.erase(it);
			  return;
		  }
	  }
  }

  void printCloudPoints(const PointCloud& cloud)
  {
    for (int i = 0; i < cloud.points.size(); i++)
    {
      ROS_INFO("Point %d: (%f, %f)", i, cloud.points[i].x, cloud.points[i].y);
    }
  }

  double distanceBtwTwoPoints(Point p1, Point p2)
  {
    return std::sqrt(std::pow(p1.x - p2.x, 2) + std::pow(p1.y - p2.y, 2));
  }


//   void calculateAndPubOval(double x1, double y1, double x2, double y2, int id)
//   {
// 	    double x = (x1 + x2) / 2;
// 	    double y = (y1 + y2) / 2;
// 	    //double dist = std::sqrt(std::pow(x1 - x2, 2) + std::pow(y1 - y2, 2));
// 	    double dist = distanceBtwTwoPoints(x1, y1, x2, y2);
// 	    double scale_x = dist + 5 * leg_radius;
// 	    double scale_y = 5 * leg_radius;
// // 	    ROS_INFO("x1 - x2: %f", std::abs(x1 - x2));
// 
// 	    double norm_1 = std::sqrt(std::pow(x1, 2) + std::pow(y1, 2));
// 	    double norm_2 = std::sqrt(std::pow(x2, 2) + std::pow(y2, 2));
// 	    double temp = (x1 * x2 + y1 * y2) / (norm_1 * norm_2);
// 
// 
// 	    double diff_x = x1 - x2;
// // 	    if (x1 > x2) { diff_x = x2 - x1; }
// 	    double diff_y = y1 - y2;
// // 	    ROS_INFO("angle_x: %f, angle_y: %f", diff_x, diff_y);
// 
// 	    double angle = std::atan2( diff_y, diff_x );
// // 	    ROS_WARN("diff_y: %f, diff_x : %f, angle: %f", diff_y, diff_x, angle);
// // 	    double angle = std::acos(temp);
// 
// 
// 	    Eigen::Quaterniond q;
// 	    q = Eigen::AngleAxisd(angle, Eigen::Vector3d::UnitZ());
// 	    double orientation_w = q.w();
// 	    double orientation_x = q.x();
// 	    double orientation_y = q.y();
// 	    double orientation_z = q.z();
// 
// 	    /*
// 	    ROS_INFO("Oval theta: %f, x: %f, y: %f, s_x: %f, s_y: %f, o_x: %f, o_y: %f, o_z: %f, o_w: %f, id: %d",
// 	      angle, x, y, scale_x, scale_y, orientation_x, orientation_y, orientation_z, orientation_w, id);*/
// 	    pub_oval(x, y, scale_x, scale_y, orientation_x, orientation_y, orientation_z, orientation_w, id);
//   }
  
  visualization_msgs::Marker getOvalMarker(int id, double x, double y, 
      double orientation_x, double orientation_y, double orientation_z, double orientation_w,
      double scale_x, double scale_y, double r, double g, double b)
  {
    visualization_msgs::Marker marker;
    marker.header.frame_id = transform_link;
    marker.header.stamp = ros::Time();
    marker.ns = nh_.getNamespace();
    marker.id = id;
    marker.type = visualization_msgs::Marker::CYLINDER;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.x = x;
    marker.pose.position.y = y;
    marker.pose.position.z = z_coordinate;
    marker.pose.orientation.x = orientation_x;
    marker.pose.orientation.y = orientation_y;
    marker.pose.orientation.z = orientation_z;
    marker.pose.orientation.w = orientation_w;
    marker.scale.x = scale_x;
    marker.scale.y = scale_y;
//     marker.scale.z = 0;
    marker.color.a = 1.0; // Don't forget to set the alpha!
    marker.color.r = r;
    marker.color.g = g;
    marker.color.b = b;
    //only if using a MESH_RESOURCE marker type:
//     marker.mesh_resource = "package://pr2_description/meshes/base_v0/base.dae";
    return marker;
  }

  visualization_msgs::Marker getOvalMarkerForTwoPoints(double x1, double y1, double x2, double y2, int id)
  {
    double x = (x1 + x2) / 2;
    double y = (y1 + y2) / 2;
    double dist = std::sqrt(std::pow(x1 - x2, 2) + std::pow(y1 - y2, 2));
    double scale_x = dist + 5 * leg_radius;
    double scale_y = 5 * leg_radius;
// 	    ROS_INFO("x1 - x2: %f", std::abs(x1 - x2));

    double norm_1 = std::sqrt(std::pow(x1, 2) + std::pow(y1, 2));
    double norm_2 = std::sqrt(std::pow(x2, 2) + std::pow(y2, 2));
    double temp = (x1 * x2 + y1 * y2) / (norm_1 * norm_2);


    double diff_x = x1 - x2;
// 	    if (x1 > x2) { diff_x = x2 - x1; }
    double diff_y = y1 - y2;
// 	    ROS_INFO("angle_x: %f, angle_y: %f", diff_x, diff_y);

    double angle = std::atan2( diff_y, diff_x );
// 	    ROS_WARN("diff_y: %f, diff_x : %f, angle: %f", diff_y, diff_x, angle);
// 	    double angle = std::acos(temp);

    Eigen::Quaterniond q;

    q = Eigen::AngleAxisd(angle, Eigen::Vector3d::UnitZ());
    double orientation_w = q.w();
    double orientation_x = q.x();
    double orientation_y = q.y();
    double orientation_z = q.z();

    /*
    ROS_INFO("Oval theta: %f, x: %f, y: %f, s_x: %f, s_y: %f, o_x: %f, o_y: %f, o_z: %f, o_w: %f, id: %d",
      angle, x, y, scale_x, scale_y, orientation_x, orientation_y, orientation_z, orientation_w, id);*/
    return getOvalMarker(id, x, y, orientation_x, orientation_y, orientation_z, orientation_w,
      scale_x, scale_y, 0.0, 0.0, 0.0);
  }

  void vis_persons()
  {
    if (people_marker_next_id != 0) {
      removeOldMarkers(people_marker_next_id, people_pub);
      people_marker_next_id = 0;
    }

    visualization_msgs::MarkerArray ma_people;

    for (int i = 0; i < persons.size(); i++) {
	    ma_people.markers.push_back(getMarker(persons[i].getPos().x,
		persons[i].getPos().y, z_coordinate * 3, getPeopleMarkerNextId()));
    }


    people_pub.publish(ma_people);
  }


  void vis_people()
  {
    if (people_marker_next_id != 0) {
      removeOldMarkers(people_marker_next_id, people_pub);
      people_marker_next_id = 0;
    }
    visualization_msgs::MarkerArray ma_people;
//     int max_id = 0;

    for (int i = 0; i < legs.size(); i++)
    {
      int id = legs[i].getPeopleId();
      if (id == -1) { continue; }
      // if (legs[i].getDistanceTravelled() < min_dist_travelled) {
      //   ROS_DEBUG("Distance travelled: not enough!");
      //   continue;
      // }

	  // second leg is removed
	if (!legs[i].hasPair())
	{
		for (Leg& l : removed_legs)
		{
			if (l.getPeopleId() == id)
			{
			  ma_people.markers.push_back(getOvalMarkerForTwoPoints(
			    legs[i].getPos().x,
			    legs[i].getPos().y,
			    l.getPos().x, 
			    l.getPos().y, 
			    getPeopleMarkerNextId()));
			  /*
			  updatePersonList(id, (legs[i].getPos().x + l.getPos().x) / 2, 
					   (legs[i].getPos().y + l.getPos().y) / 2);*/
// 			  max_id++;

// 			  ROS_INFO("VISpeople peopleId: %d, pos1: (%f, %f), pos2removed: (%f, %f), predictions: (%d, %d), observations: (%d, %d), hasPair: (%d, %d)",
// 			  id, legs[i].getPos().x, legs[i].getPos().y, l.getPos().x, l.getPos().y, legs[i].getPredictions(), l.getPredictions(),
// 				   legs[i].getObservations(), l.getObservations(), legs[i].hasPair(), l.hasPair());

			  break;
			}
		}

	} 
	else 
	{
	  for (int j = i + 1; j < legs.size(); j++)
	  {
	    if (legs[j].getPeopleId() == id)
	    {
	      ma_people.markers.push_back(getOvalMarkerForTwoPoints(legs[i].getPos().x,
		  legs[i].getPos().y, legs[j].getPos().x, legs[j].getPos().y, getPeopleMarkerNextId()));
	      break;

	      /*
	      updatePersonList(id, (legs[i].getPos().x + l.getPos().x) / 2, 
		  (legs[i].getPos().y + l.getPos().y) / 2);*/

  // 	    max_id++;

  // 	    ROS_INFO("VISpeople peopleId: %d, pos1: (%f, %f), pos2: (%f, %f), predictions: (%d, %d), observations: (%d, %d), hasPair: (%d, %d)",
  // 	    id, legs[i].getPos().x, legs[i].getPos().y,
  // 		     legs[j].getPos().x, legs[j].getPos().y,
  // 		     legs[i].getPredictions(), legs[j].getPredictions(),
  // 		      legs[i].getObservations(), legs[j].getObservations(),
  // 		     legs[i].hasPair(), legs[j].hasPair());
	    }
	  }
      }
    }

    people_pub.publish(ma_people);
  }
  /*
  void updatePersonList(int id, double x, double y)
  {
    
  }*/


  unsigned int getPeopleMarkerNextId() {
    return people_marker_next_id++;
  }

  bool findAndEraseMatch(const Point& searchPoint, PointCloud& cloud, Point& out, double radius)
  {
    if (cloud.points.size() == 0) { return false; }
    pcl::KdTreeFLANN<Point> kdtree;
    kdtree.setInputCloud (cloud.makeShared());
    // std::vector<int> pointIdxRadius;
    // std::vector<float> pointsSquaredDistRadius;
    // // radius search
    // int count = kdtree.radiusSearch (searchPoint, radius, pointIdxRadius, pointsSquaredDistRadius);
    // if (count == 0) { return false; }
    // if (count > 1)
    // {
    //   //interesting more than one point matched
    // }
    int K = 1;
    std::vector<int> pointsIdx(K);
    std::vector<float> pointsSquaredDist(K);
    kdtree.nearestKSearch (searchPoint, K, pointsIdx, pointsSquaredDist);
    if (pointsSquaredDist[0] > radius && pointsSquaredDist[0] < 0.5 * leg_radius) {
      return false;
    }
    out = cloud.points[pointsIdx[0]];
    cloud.points.erase(cloud.points.begin() + pointsIdx[0]);
    return true;
  }



  void computeKalmanFilterPredictions(PointCloud& predictions)
  {
//     for (Leg& l : legs)
//     {
//       predictions.points.push_back(l.computePrediction());
//     }
  }
  
  
  void pub_cloud_points_as_line(const PointCloud& cloud) 
  {
    visualization_msgs::Marker points, line_strip, line_list;
    points.header.frame_id = line_strip.header.frame_id = line_list.header.frame_id = transform_link;
    points.header.stamp = line_strip.header.stamp = line_list.header.stamp = ros::Time::now();
    points.ns = line_strip.ns = line_list.ns = nh_.getNamespace();
    points.action = line_strip.action = line_list.action = visualization_msgs::Marker::ADD;
    points.pose.orientation.w = line_strip.pose.orientation.w = line_list.pose.orientation.w = 1.0;

    points.id = 0;
    line_strip.id = 1;
    line_list.id = 2;

    points.type = visualization_msgs::Marker::POINTS;
    line_strip.type = visualization_msgs::Marker::LINE_STRIP;
    line_list.type = visualization_msgs::Marker::LINE_LIST;

    points.scale.x = 0.2;
    points.scale.y = 0.2;
    
    
    // LINE_STRIP/LINE_LIST markers use only the x component of scale, for the line width
    line_strip.scale.x = 0.1;
    line_list.scale.x = 0.1;
// %EndTag(SCALE)%

// %Tag(COLOR)%
    // Points are green
    points.color.g = 1.0f;
    points.color.a = 1.0;

    // Line strip is blue
    line_strip.color.b = 1.0;
    line_strip.color.a = 1.0;

    // Line list is red
    line_list.color.r = 1.0;
    line_list.color.a = 1.0;
// %EndTag(COLOR)%

// %Tag(HELIX)%
    // Create the vertices for the points and lines
    for (Point cloud_p : cloud.points)
    {
      geometry_msgs::Point p;
      p.x = cloud_p.x;
      p.y = cloud_p.y;
      p.z = cloud_p.z;

      points.points.push_back(p);
      line_strip.points.push_back(p);

      // The line list needs two points for each line
      line_list.points.push_back(p);
      p.z += 1.0;
      line_list.points.push_back(p);
    }
// %EndTag(HELIX)%

    marker_pub.publish(points);
    marker_pub.publish(line_strip);
    marker_pub.publish(line_list);
    
    
  }
  
  void pubWithZCoordinate(PointCloud cloud) 
  {
    for (Point& p : cloud)
    {
      p.z = 0.178;
    }
    pcl_cloud_publisher.publish(cloud);
  }

  bool clustering(const PointCloud& cloud, PointCloud& cluster_centroids)
  {
//     pcl_cloud_publisher.publish(cloud);
//     pubWithZCoordinate(cloud);
//     pub_cloud_points_as_line(cloud);

    if (cloud.points.size() < minClusterSize) { ROS_DEBUG("Clustering: Too small number of points!"); return false; }

    pcl::search::KdTree<Point>::Ptr tree (new pcl::search::KdTree<Point>);
    tree->setInputCloud (cloud.makeShared());
    std::vector<pcl::PointIndices> cluster_indices;
    pcl::EuclideanClusterExtraction<Point> ec;
    ec.setClusterTolerance (clusterTolerance); 
    ec.setMinClusterSize (minClusterSize);
    ec.setMaxClusterSize (maxClusterSize);
    ec.setSearchMethod(tree);
    ec.setInputCloud(cloud.makeShared());
    ec.extract(cluster_indices);

    cluster_centroids.header = cloud.header;
    cluster_centroids.points.clear();
    
    PointCloud cluster_centroids_temp, leg_positions;
    cluster_centroids_temp.header = cloud.header;
    leg_positions.header = cloud.header;
    
    std::vector<std::pair<Point, Point> > minMaxPoints;
    double minMaxUncertainty = 0.04;
    
    for (Leg& l : legs) 
    {
//       if (calculateNorm(l.getVel()) < 0.4)
      if (l.getPeopleId() != -1)
      {
	leg_positions.points.push_back(l.getPos());
      }
    }
    pcl::KdTreeFLANN<Point> kdtree_clusters, kdtree_legs;
    
    if (leg_positions.points.size() != 0) {
      kdtree_legs.setInputCloud(leg_positions.makeShared());
    }

    for (std::vector<pcl::PointIndices>::const_iterator it = cluster_indices.begin(); it != cluster_indices.end(); ++it)
    {
      pcl::PointCloud<Point>::Ptr cloud_cluster (new pcl::PointCloud<Point>);
      cloud_cluster->header = cloud.header;
      for (std::vector<int>::const_iterator pit = it->indices.begin (); pit != it->indices.end (); ++pit)
	cloud_cluster->points.push_back (cloud.points[*pit]); //*
      cloud_cluster->width = cloud_cluster->points.size();
      cloud_cluster->height = 1;
      cloud_cluster->is_dense = true;

//       std::vector<double> c;
      //computeCircularity(cloud_cluster, c);
      
      Point min, max;
      pcl::getMinMax3D(*cloud_cluster, min, max);
      min.x -= minMaxUncertainty;
      min.y -= minMaxUncertainty; 
      max.x += minMaxUncertainty; 
      max.y += minMaxUncertainty; 
      minMaxPoints.push_back(std::make_pair(min, max));
      
      
      Eigen::Vector4f centroid;
      pcl::compute3DCentroid(*cloud_cluster, centroid);
      
    
      Point p; p.x = centroid(0); p.y = centroid(1);
      
      
      
      if (got_map) {
	
	bool isPointTransformed = true;
	geometry_msgs::PointStamped point_in, point_out;
	
	point_in.header.frame_id = cloud.header.frame_id;
	point_in.header.stamp = ros::Time::now();
	point_in.point.x = p.x; 
	point_in.point.y = p.y;
	
	geometry_msgs::TransformStamped transformStamped;
	try{
// 	  tfBuffer.transform(point_in, point_out, local_map.header.frame_id);
	  transformStamped = tfBuffer.lookupTransform(local_map.header.frame_id, point_in.header.frame_id, ros::Time(0));
	}
	catch (tf2::TransformException &ex) {
	  ROS_WARN("Failure to lookup the transform for a point! %s\n", ex.what());
	  isPointTransformed = false;
	}
	tf2::doTransform(point_in, point_out, transformStamped);
    
	if (isPointTransformed) {
	  double in_free_space = how_much_in_free_space(point_out.point.x, point_out.point.y);
	  ROS_WARN("in_free_space for (%f, %f): %f", point_out.point.x, point_out.point.y, in_free_space);
	  if (in_free_space > in_free_space_threshold) 
	  {
	    continue;
	  } 
	}
      }
      
      if (leg_positions.points.size() != 0) {
	std::vector<int> pointIdxRadius;
	std::vector<float> pointsSquaredDistRadius;
	// radius search
	int count = kdtree_legs.radiusSearch(p, 0.03, pointIdxRadius, 
							  pointsSquaredDistRadius);
	if (count == 1) {
	  cluster_centroids_temp.points.push_back(leg_positions.points[pointIdxRadius[0]]);
	} else {
	  cluster_centroids_temp.points.push_back(p);
	}
      } else {
	cluster_centroids.points.push_back(p);
      }
    }
    
    
//     ROS_WARN("\ncluster_centroids_temp\n");
//     printCloudPoints(cluster_centroids_temp);
//     
//     ROS_WARN("\nleg_positions\n");
//     printCloudPoints(leg_positions);
//     
//     ROS_WARN("\ncluster_centroids\n");
//     printCloudPoints(cluster_centroids);
    
    if (cluster_centroids_temp.points.size() == 0) { return true; }
    
    kdtree_clusters.setInputCloud(cluster_centroids_temp.makeShared());
    
    
    double radius = 0.2;
    
    std::map<int, bool> map_removed_indices;
//     std::map<int, bool> map_removed_leg_indices;
    
    for (int i = 0; i < cluster_centroids_temp.points.size(); i++)
    {
      std::map<int, bool>::iterator removed_indices_it = map_removed_indices.find(i);
      if (removed_indices_it != map_removed_indices.end()) { continue; }
      
      Point cluster = cluster_centroids_temp.points[i];
      std::vector<int> pointIdxRadius_clusters, pointIdxRadius_legs;
      std::vector<float> pointsSquaredDistRadius_clusters, pointsSquaredDistRadius_legs;
      
      int K = 2;
      std::vector<int> pointsIdx(K);
      std::vector<float> pointsSquaredDist(K);
      
      
//       radius search
      int count_clusters = kdtree_clusters.radiusSearch(cluster, radius, pointIdxRadius_clusters, 
							pointsSquaredDistRadius_clusters);
      
//       int count_legs = kdtree_legs.radiusSearch(cluster, radius, pointIdxRadius_legs, 
// 						pointsSquaredDistRadius_legs);
      int count_legs = kdtree_legs.nearestKSearch(cluster, K, pointsIdx, pointsSquaredDist);
      
      
      if (pointsIdx.size() != K) { 
	cluster_centroids.points.push_back(cluster); 
	continue; 
      }
      
//       std::map<int, bool>::iterator removed_leg_indices_it = map_removed_leg_indices.find(pointsIdx[0]);
//       if (removed_indices_it != map_removed_leg_indices.end()) { 
// 	cluster_centroids.points.push_back(cluster); 
// 	continue;
//       }
      
//       removed_leg_indices_it = map_removed_leg_indices.find(pointsIdx[1]);
//       if (removed_indices_it != map_removed_leg_indices.end()) { 
// 	cluster_centroids.points.push_back(cluster); 
// 	continue;
//       }
      
      
      Point fst_leg, snd_leg;
      fst_leg = leg_positions[pointsIdx[0]];
      snd_leg = leg_positions[pointsIdx[1]];
      bool isFstPointInBox = (fst_leg.x >= minMaxPoints[i].first.x) && (fst_leg.y >= minMaxPoints[i].first.y)
	&& (fst_leg.x <= minMaxPoints[i].second.x) && (fst_leg.y <= minMaxPoints[i].second.y);
      bool isSndPointInBox = (snd_leg.x >= minMaxPoints[i].first.x) && (snd_leg.y >= minMaxPoints[i].first.y)
	&& (snd_leg.x <= minMaxPoints[i].second.x) && (snd_leg.y <= minMaxPoints[i].second.y);
      
//       ROS_INFO("\n\ndivide:");
//       ROS_INFO("Point fst_leg: (%f, %f)", fst_leg.x, fst_leg.y);
//       ROS_INFO("Point snd_leg: (%f, %f)", snd_leg.x, snd_leg.y);
	
	
	
//       ROS_WARN("i: %d, count_clusters: %d, count_legs: %d", i, count_clusters, count_legs);
      
//       if (count_clusters < count_legs) 
      if (isFstPointInBox && isSndPointInBox)
      { // divide
	
// 	ROS_INFO("\n\ndivide happens:");
// 	pub_border(minMaxPoints[i].first.x, minMaxPoints[i].first.y, 
// 	  minMaxPoints[i].second.x, minMaxPoints[i].second.y);
	
	
// 	map_removed_leg_indices.insert(std::make_pair(pointsIdx[0], true));
// 	map_removed_leg_indices.insert(std::make_pair(pointsIdx[1], true));
	
	
	std::vector<pcl::PointIndices>::const_iterator it = cluster_indices.begin();
	it += i;
// 	std::vector<int>::const_iterator pit_front = it->indices.begin(),
// 					 pit_back = it->indices.end(); pit_back--;
// 	double dist_front_last = distanceBtwTwoPoints(cloud.points[*pit_front], cloud.points[*pit_back]);
// 	ROS_WARN("dist_front_last: %f", dist_front_last);
// 	if (it->indices.size() < 25 || 
// 	  dist_front_last < radius) 
// 	{
// 	  cluster_centroids.points.push_back(cluster);
// 	  continue;
// 	}
	
	
	
	PointCloud fst, snd;
	fst.header = cloud.header;
	snd.header = cloud.header;
	for (std::vector<int>::const_iterator pit = it->indices.begin(); pit != it->indices.end(); ++pit)
	{ 
	  Point p = cloud.points[*pit];
	  double dot_product = p.x * cluster.y - cluster.x * p.y;
	  if (dot_product < 0) { fst.points.push_back(p); }
	  else { snd.points.push_back(p); }
	}
// 	ROS_WARN("fst.points.size: %d, snd.points.size: %d", (int) fst.points.size(), (int) snd.points.size());
	
	if (fst.points.size() < minClusterSize || snd.points.size() < minClusterSize) 
	{
	  cluster_centroids.points.push_back(cluster);
	  continue;
	}
	
	Eigen::Vector4f centroid_fst, centroid_snd;
	pcl::compute3DCentroid(fst, centroid_fst);
	pcl::compute3DCentroid(snd, centroid_snd);
	
	Point p_fst, p_snd;
	p_fst.x = centroid_fst(0); p_fst.y = centroid_fst(1);
	p_snd.x = centroid_snd(0); p_snd.y = centroid_snd(1);
	  
// 	ROS_INFO("Point fst: (%f, %f)", p_fst.x, p_fst.y);
// 	ROS_INFO("Point snd: (%f, %f)", p_snd.x, p_snd.y);
// 	ROS_INFO("distanceBtwTwoPoints(p_fst, p_snd): %f", distanceBtwTwoPoints(p_fst, p_snd));
	
	
	if (distanceBtwTwoPoints(p_fst, p_snd) < leg_radius) 
	{
	  cluster_centroids.points.push_back(cluster);
	  continue;
	}
	
	cluster_centroids.points.push_back(p_fst);
	cluster_centroids.points.push_back(p_snd);
      }
//       else if (count_clusters > count_legs)
//       { // bring together
// 	PointCloud gathered_clusters;
// 	gathered_clusters.header = cloud.header;
// 	
// 	for (int j : pointIdxRadius_clusters) 
// 	{
// 	  if (j > 0 && gathered_clusters.points.size() > 0 && j > i) 
// 	  { 
// 	    if (distanceBtwTwoPoints(gathered_clusters.points[0], 
// 	    cluster_centroids_temp.points[j]) >= leg_radius) 
// 	    {
// 	      continue;
// 	    }
// 	  }
// 	  map_removed_indices.insert(std::make_pair(j, true));
// 	  gathered_clusters.points.push_back(cluster_centroids_temp.points[j]);
// 	}
// 	
// 	Eigen::Vector4f centroid;
// 	pcl::compute3DCentroid(gathered_clusters, centroid);
// 	Point p; p.x = centroid(0); p.y = centroid(1);
// 	
// 	cluster_centroids.points.push_back(p);
//       }
      else
      {
	cluster_centroids.points.push_back(cluster);
      }
    }
//     ROS_WARN("\ncluster_centroids at end\n");
//     printCloudPoints(cluster_centroids);
    pubWithZCoordinate(cluster_centroids);
//     pcl_cloud_publisher.publish(cluster_centroids);
    return true;
  }
  
    void pub_border(double min_x, double min_y, double max_x, double max_y)
  {
    visualization_msgs::Marker marker;
    marker.header.frame_id = transform_link;
    marker.header.stamp = ros::Time();
    marker.ns = nh_.getNamespace();
//     marker.id = id;
    marker.type = visualization_msgs::Marker::CUBE;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.x = (max_x + min_x) / 2;
    marker.pose.position.y = (max_y + min_y) / 2;
    marker.pose.position.z = z_coordinate;
//     marker.pose.orientation.x = orientation_x;
//     marker.pose.orientation.y = orientation_y;
//     marker.pose.orientation.z = orientation_z;
//     marker.pose.orientation.w = orientation_w;
    marker.scale.x = max_x - min_x;
    marker.scale.y = max_y - min_y;
//     marker.scale.z = 0;
    marker.color.a = 1.0; // Don't forget to set the alpha!
    marker.color.g = 1.0;
//     if (id == 0)
//       marker.color.r = 1.0;
//     if (id == 2)
//       marker.color.g = 1.0;
//     if (id == 1)
//       marker.color.b = 1.0;
    //only if using a MESH_RESOURCE marker type:
//     marker.mesh_resource = "package://pr2_description/meshes/base_v0/base.dae";
    tracking_area_pub.publish( marker );
  }
  

  /*void sortPointCloudToLeftAndRight(const PointCloud& input_cloud, PointCloud::Ptr left, PointCloud::Ptr right)
  {

//     #if CV_MAJOR_VERSION == 2
    // do opencv 2 code


    // convert input cloud to opencv Mat
    cv::Mat points(input_cloud.size(),1, CV_32FC3);
    std::size_t idx = 0;

//     std::cout << "Input model points:\n";
    for(PointCloud::const_iterator it = input_cloud.begin(); it != input_cloud.end(); ++it, ++idx)
    {
	    points.at<cv::Vec3f>(idx,0) = cv::Vec3f(it->x,it->y);
	    // std::cerr << points.at<cv::Vec3f>(0,idx) << std::endl;
    }

    // reshape to

    cv::Mat labels;
    // cv::Mat(sampleCount,1,CV_32S);


    int attempts = 10;
    cv::Mat centers;

    int max_cluster_size = input_cloud.size() > cluster_size ? cluster_size : input_cloud.size();

    // use opencv kmeans to extract the cluster center, since pcl 1.7.2 does not have kmeans
    cv::kmeans(points, max_cluster_size, labels,
	    cv::TermCriteria(CV_TERMCRIT_ITER|CV_TERMCRIT_EPS, 10e4, 1e-4),
	    attempts, cv::KMEANS_RANDOM_CENTERS, centers);

    if (centers.rows != 2)
    {
      ROS_INFO("KMeans: The number of rows is not valid!");
      return;
    }


    cv::Vec3f cv_center1 = centers.at<cv::Vec3f>(0);
    cv::Vec3f cv_center2 = centers.at<cv::Vec3f>(1);

    cv::Point2f c1(cv_center1[0], cv_center1[1]);
    cv::Point2f c2(cv_center2[0], cv_center2[1]);

    double dist_to_center1 = cv::norm(left_leg_prediction - c1);
    double dist_to_center2 = cv::norm(left_leg_prediction - c2);

    int leftId = dist_to_center1 < dist_to_center2 ? 0 : 1;

    // compare two centers
    // cv_center1[0],cv_center1[1] && cv_center2[0],cv_center2[1]

    // for example
    // is y of the first center bigger than y of the second center?
//     int leftId = cv_center1[1] > cv_center2[1] ? 0 : 1;
//     ROS_INFO("0: %f, 1: %f, leftId: %d", cv_center1[1], cv_center2[1], leftId);

    int i = 0;
    for(PointCloud::const_iterator it = input_cloud.begin(); it != input_cloud.end(); ++it, ++i)
    {
      int id = labels.at<int>(i,0);
      if (id == leftId)
      {
	left->points.push_back(*it);
      }
      else
      {
	right->points.push_back(*it);
      }
    }

//     #elif CV_MAJOR_VERSION == 3
    // do opencv 3 code
//     #endif
  }*/

//   std::vector<double> computeCentroids(PointCloud::Ptr cloud)
//   {
//     std::vector<double> result;
//     Eigen::Vector4f centroid;
//     pcl::compute2DCentroid (*cloud, centroid);
//     result.push_back(centroid(0));
//     result.push_back(centroid(1));
//     return result;
//   }

  std::vector<double> computeRansacPubInliersAndGetCenters(PointCloud::Ptr cloud)
  {
    std::vector<double> centers;
    PointCloud toPub;
    toPub.header = cloud->header;

    pcl::SampleConsensusModelCircle2D<Point>::Ptr model(
      new pcl::SampleConsensusModelCircle2D<Point> (cloud));

    pcl::RandomSampleConsensus<Point> ransac (model);

    ransac.setDistanceThreshold (ransac_dist_threshold);

    try
    {
      ransac.computeModel();
    }
    catch (...)
    {
      ROS_INFO("Ransac: Computing model has failed!");
      return centers;
    }

    /*
     * get the radius of the circles
     */
    Eigen::VectorXf circle_coeff;

    std::vector<int> inliers;
    ransac.getInliers(inliers);
    if (inliers.size() < 3)
    {
      ROS_INFO("The number of inliers is too small!");
      return centers;
    }

    for (int i : inliers)
    {
      toPub.points.push_back(cloud->points[i]);
    }
    ransac.getModelCoefficients(circle_coeff);

    centers.push_back(circle_coeff(1));
    centers.push_back(circle_coeff(2));


    Point point;
    point.x = circle_coeff(0);
    point.y = circle_coeff(1);
    toPub.points.push_back(point);

    pcl_cloud_publisher.publish(toPub.makeShared());

    return centers;
  }

  void pub_leg_posvelacc(std::vector<double>& in, bool isLeft)
  {
    if (in.size() != 6) { ROS_ERROR("Invalid vector of leg posvelacc!"); return; }

    std_msgs::Float64MultiArray msg;

    // set up dimensions
    msg.layout.dim.push_back(std_msgs::MultiArrayDimension());
    msg.layout.dim[0].size = in.size();
    msg.layout.dim[0].stride = 1;
    msg.layout.dim[0].label = "pos_vel_acc";

    // copy in the data
    msg.data.clear();
    msg.data.insert(msg.data.end(), in.begin(), in.end());

    if (isLeft) { pos_vel_acc_lleg_pub.publish(msg); }
    else { pos_vel_acc_rleg_pub.publish(msg); }
  }

//   bool useKalmanFilterAndPubCircles(const PointCloud::Ptr cloud, bool isLeft)
//   {
//     const int num_elements_measurement = 2;
//     const int num_elements_kalman = 6;
//     std::vector<double> centerOfLegMeasurement;
//     std::vector<double> centerOfLegKalman;
// //     if (circle_fitting == "ransac")
// //     {
// //       centerOfLegMeasurement = computeRansacPubInliersAndGetCenters(cloud);
// //     }
// //     else
// //     {
// //       centerOfLegMeasurement = computeCentroids(cloud);
// //     }
//     if (!computeCircularity(cloud, centerOfLegMeasurement)) { return false; }
// 
//     if (centerOfLegMeasurement.size() < num_elements_measurement)
//     {
//       ROS_ERROR("Center (measurement) does not have enough elements!");
//       return false;
//     }
// 
// //     if (abs(centerOfLegMeasurement[0] - centerOfLegLastMeasurement[0]) > ...)
// //     {
// //
// //     }
//     std::vector<double> prediction;
//     if (isLeft) { left_leg_filter->predict(prediction); left_leg_prediction = cv::Point2f(prediction[0], prediction[1]); }
//     else { right_leg_filter->predict(prediction); right_leg_prediction = cv::Point2f(prediction[0], prediction[1]); }
//     pub_circle(prediction[0], prediction[1], leg_radius, isLeft, isLeft ? 2 : 3);
// 
//     if (isLeft) { left_leg_filter->update(centerOfLegMeasurement, centerOfLegKalman); }
//     else { right_leg_filter->update(centerOfLegMeasurement, centerOfLegKalman); }
// 
// //     pub_circle(centerOfLegMeasurement[0], centerOfLegMeasurement[1], leg_radius, isLeft, isLeft ? 0 : 1);
// 
// 
// 
//     if (centerOfLegKalman.size() < num_elements_kalman)
//     {
//       ROS_ERROR("Centers (kalman) do not have enough elements!");
//       return false;
//     }
// 
// //     pub_circle(centerOfLegKalman[0], centerOfLegKalman[1], leg_radius, isLeft, isLeft ? 2 : 3);
//     if (legs_gathered == 1) {
//       person_center.x = (person_center.x + centerOfLegKalman[0]) / 2;
//       person_center.y = (person_center.y + centerOfLegKalman[1]) / 2;
//       legs_gathered++;
//     } else {
//       if (legs_gathered == 2) {
// 	pub_circle(person_center.x, person_center.y, 0.7, isLeft, 0);
//       }
//       person_center.x = centerOfLegKalman[0];
//       person_center.y = centerOfLegKalman[1];
//       legs_gathered = 1;
//     }
// 
//     pub_leg_posvelacc(centerOfLegKalman, isLeft);
// 
//     return true;
//   }

  bool computeCircularity(const PointCloud::Ptr cloud, std::vector<double>& center)
  {
    int num_points = cloud->points.size();
    if (num_points < minClusterSize) { ROS_ERROR("Circularity and Linerity: Too small number of points!"); return false; }
    double x_mean, y_mean;
    CvMat* A = cvCreateMat(num_points, 3, CV_64FC1);
    CvMat* B = cvCreateMat(num_points, 1, CV_64FC1);

//     CvMat* points = cvCreateMat(num_points, 2, CV_64FC1);

    int j = 0;
    for (Point p : cloud->points)
    {
      x_mean += p.x / num_points;
      y_mean += p.y / num_points;

//       cvmSet(points, j, 0, p.x - x_mean);
//       cvmSet(points, j, 1, p.y - y_mean);

      cvmSet(A, j, 0, -2.0 * p.x);
      cvmSet(A, j, 1, -2.0 * p.y);
      cvmSet(A, j, 2, 1);

      cvmSet(B, j, 0, -pow(p.x, 2) - pow(p.y, 2));
      j++;
    }

//     CvMat* W = cvCreateMat(2, 2, CV_64FC1);
//     CvMat* U = cvCreateMat(num_points, 2, CV_64FC1);
//     CvMat* V = cvCreateMat(2, 2, CV_64FC1);
//     cvSVD(points, W, U, V);
//
//     CvMat* rot_points = cvCreateMat(num_points, 2, CV_64FC1);
//     cvMatMul(U, W, rot_points);
//
//     // Compute Linearity
//     double linearity = 0.0;
//     for (int i = 0; i < num_points; i++)
//     {
//       linearity += pow(cvmGet(rot_points, i, 1), 2);
//     }
//
//     cvReleaseMat(&points);
//     points = 0;
//     cvReleaseMat(&W);
//     W = 0;
//     cvReleaseMat(&U);
//     U = 0;
//     cvReleaseMat(&V);
//     V = 0;
//     cvReleaseMat(&rot_points);
//     rot_points = 0;



    // Compute Circularity

    CvMat* sol = cvCreateMat(3, 1, CV_64FC1);

    cvSolve(A, B, sol, CV_SVD);

    double xc = cvmGet(sol, 0, 0);
    double yc = cvmGet(sol, 1, 0);
    double rc = sqrt(pow(xc, 2) + pow(yc, 2) - cvmGet(sol, 2, 0));

    center.clear();
    center.push_back(xc);
    center.push_back(yc);

    return true;

//     pub_circle(xc, yc, rc, isLeft, isLeft ? 0 : 1);

//     cvReleaseMat(&A);
//     A = 0;
//     cvReleaseMat(&B);
//     B = 0;
//     cvReleaseMat(&sol);
//     sol = 0;
//
//     float circularity = 0.0;
//     for (SampleSet::iterator i = cluster->begin();
// 	i != cluster->end();
// 	i++)
//     {
//       circularity += pow(rc - sqrt(pow(xc - (*i)->x, 2) + pow(yc - (*i)->y, 2)), 2);
//     }

  }

   void predictLegs()
   {
     for (Leg& l : legs) {
       l.predict();
     }
     cullDeadTracks(legs);
   }

   void removeLegFromVector(std::vector<Leg>& v, int i)
   {
      if (i != v.size() - 1) {
	Leg l = v[v.size() - 1];
	v[i] = l;
      }
      v.pop_back();
   }

//    void removePersonFromVector(std::vector<Person>& v, int i)
//    {
//       if (i != v.size() - 1) {
// 	Person p = v[v.size() - 1];
// 	v[i] = p;
//       }
//       v.pop_back();
//    }

   void resetHasPair(std::vector<Leg>& v, int fst_leg)
   {
      for (int snd_leg = 0; snd_leg < v.size(); snd_leg++) {
	if (snd_leg != fst_leg && v[snd_leg].getPeopleId() == v[fst_leg].getPeopleId()) {
	  ROS_WARN("heir fst: %d, snd: %d", fst_leg, snd_leg);
	  v[snd_leg].setHasPair(false);
	  break;
	}
      }
   }

   void cullDeadTracks(std::vector<Leg>& v)
   {
     int i = 0;
     while(i < v.size()) {
//        if (i >= size) { return; }
      ROS_INFO("cull while i: %d, v.size: %d", i, (int) v.size());
      ROS_INFO("dead: %d", v[i].getOccludedAge()); 
      
      if (v[i].is_dead() || v[i].getCov() > max_cov   /*|| 
      v[i].getPos().x < x_lower_limit || v[i].getPos().x > x_upper_limit ||
	v[i].getPos().y < y_lower_limit || v[i].getPos().y > y_upper_limit*/) {
	ROS_INFO("cull before removed_legs.push_back %d", i);
	  if (v[i].hasPair()) { resetHasPair(v, i); removed_legs.push_back(v[i]); }
// 	    else if (v[i].getPeopleId() != -1) { removed_legs.push_back(v[i]); }
	ROS_INFO("cull after removed_legs.push_back");
	printLegsInfo(v, "v");
	  removeLegFromVector(v, i);
	ROS_INFO("cull after removeLegFromVector");
	printLegsInfo(v, "v");
      } else {
	i++;
      }
    }		
  }

//    void cullDeadTracksOfPersons(std::vector<Person>& v)
//    {
//      int i = 0;
//      while(i != v.size()) {
//         if (v[i].is_dead()) {
// 	    removePersonFromVector(v, i);
//         } else {
//              i++;
//         }
//      }
//    }


//    void gnn_munkres_people(const PointCloud& new_people, std::vector<int>& new_people_idx)
//    {
//       // Filter model predictions
//       for(std::vector<Person>::iterator it = persons.begin();
//           it != persons.end(); it++) {
//            it->predict();
//       }
//       std::vector<Person> fused;
// 
//       assign_munkres_people(new_people, persons, fused, new_people_idx);
// 
//       cullDeadTracksOfPersons(fused);
// 
//       persons = fused;
//    }

//    void assign_munkres_people(const PointCloud& new_people,
//                          std::vector<Person> &tracks,
//                          std::vector<Person> &fused, std::vector<int>& new_people_idx)
//    {
//      // Create cost matrix between previous and current blob centroids
//        int meas_count = new_people.points.size();
//        int tracks_count = tracks.size();
// 
//        // Determine max of meas_count and tracks
//        int rows = -1, cols = -1;
//        if (meas_count >= tracks_count) {
//             rows = cols = meas_count;
//        } else {
//             rows = cols = tracks_count;
//        }
// 
//        Matrix<double> matrix(rows, cols);
// 
//        // New measurements are along the Y-axis (left hand side)
//        // Previous tracks are along x-axis (top-side)
//        int r = 0;
//        for(const Point& p : new_people.points) {
//             std::vector<Person>::iterator it_prev = tracks.begin();
//             int c = 0;
//             for (; it_prev != tracks.end(); it_prev++, c++) {
//               // if (it_prev->isPerson() and not it_prev->isInFreeSpace()) {
//               //   matrix(r,c) = max_cost;
//               // } else {
//                 double cov = it_prev->getMeasToTrackMatchingCov();
//                 double mahalanobis_dist = std::sqrt((std::pow((p.x - it_prev->getPos().x), 2) +
//                                                      std::pow((p.y - it_prev->getPos().y), 2)) / cov);
// 
//                 if (mahalanobis_dist < mahalanobis_dist_gate) {
//                   matrix(r,c) = mahalanobis_dist;
//                 } else {
//                   matrix(r,c) = max_cost;
//                 }
//               // }
//             }
//             r++;
//        }
// 
//        Munkres<double> m;
//        m.solve(matrix);
// 
//        // Use the assignment to update the old tracks with new blob measurement
//        int meas_it = 0;
//        for(r = 0; r < rows; r++) {
//             std::vector<Person>::iterator it_prev = tracks.begin();
//             for (int c = 0; c < cols; c++) {
//                  if (matrix(r,c) == 0) {
//                       if (r < meas_count && c < tracks_count) {
//                            // Does the measurement fall within 3 std's of
//                            // the track?
// 			    if (
// 			if (it_prev->is_within_region(new_people.points[meas_it], 3)) {
//                                 // Found an assignment. Update the new measurement
//                                 // with the track ID and age of older track. Add
//                                 // to fused list
//                                 //it->matched_track(*it_prev);
//                                 it_prev->update(new_people.points[meas_it]);
//                                 fused.push_back(*it_prev);
//                            } else {
//                                 // TOO MUCH OF A JUMP IN POSITION
//                                 // Probably a missed track or a new track
//                                 it_prev->missed();
//                                 fused.push_back(*it_prev);
// 
//                                 // And a new track
//                                 fused.push_back(initPerson(new_people.points[meas_it], new_people_idx[meas_it]));
//                            }
//                       } else if (r >= meas_count) {
//                            it_prev->missed();
//                            fused.push_back(*it_prev);
//                       } else if (c >= tracks_count) {
//                            // Possible new track
//                            fused.push_back(initPerson(new_people.points[meas_it], new_people_idx[meas_it]));
//                       }
//                       break; // There is only one assignment per row
//                  }
//                  if (c < tracks_count-1) {
//                       it_prev++;
//                  }
//             }
//             if (r < meas_count-1) {
//                  meas_it++;
//             }
//        }
//    }


    
    int getNextCovEllipseId()
    {
      return cov_ellipse_id++;
    }

    void gnn_munkres(PointCloud& cluster_centroids)
    {
      
      // Filter model predictions
      for (int i = 0; i < legs.size(); i++)
      {	
	if (legs[i].hasPair()) {
	  double vel = calculateNorm(legs[i].getVel());
	  if (vel > 0.2) {
	    for (int j = i + 1; j < legs.size(); j++) {
	      if (legs[i].getPeopleId() == legs[j].getPeopleId()) {
		double dist = distanceBtwTwoPoints(legs[i].getPos(), legs[j].getPos());
		if (max_dist_btw_legs - dist < 0.1) {
		  legs[i].resetCovAndState();
		}
		break;
	      }
	    }
	  }
	}
	legs[i].predict();
      }
//     ROS_INFO("m1");
      
      if (cluster_centroids.points.size() == 0) { return; }
      
      std::vector<Leg> fused;
//     ROS_INFO("m2");

      assign_munkres(cluster_centroids, legs, fused);
//     ROS_INFO("m3");

      cullDeadTracks(fused);
//     ROS_INFO("m4");
      
      legs = fused;
//     ROS_INFO("m5");
    }

    void assign_munkres(const PointCloud& meas,
		      std::vector<Leg> &tracks,
		      std::vector<Leg> &fused)
    {
      // Create cost matrix between previous and current blob centroids
      int meas_count = meas.points.size();
      int tracks_count = tracks.size();
//       ROS_INFO("a1");

      // Determine max of meas_count and tracks
      int rows = -1, cols = -1;
      if (meas_count >= tracks_count) {
	  rows = cols = meas_count;
      } else {
	  rows = cols = tracks_count;
      }
//       ROS_INFO("a2");

      Matrix<double> matrix(rows, cols);
//       ROS_INFO("a3");
      
      visualization_msgs::MarkerArray cov_ellipse_ma;
//       ROS_INFO("a4");
      if (cov_ellipse_id != 0) {
	removeOldMarkers(cov_ellipse_id, cov_marker_pub);
	cov_ellipse_id = 0;
      }
//       ROS_INFO("a5");

      // New measurements are along the Y-axis (left hand side)
      // Previous tracks are along x-axis (top-side)
      int r = 0;
      for(const Point& p : meas.points) {
// 	  ROS_INFO("a6");
	  std::vector<Leg>::iterator it_prev = tracks.begin();
	  int c = 0;
	  for (; it_prev != tracks.end(); it_prev++, c++) {
	    // if (it_prev->isPerson() and not it_prev->isInFreeSpace()) {
	    //   matrix(r,c) = max_cost;
	    // } else {
	      Eigen::MatrixXd cov_matrix = it_prev->getMeasToTrackMatchingCovMatrix();
// 	  ROS_INFO("a7");
// 	      std::cout << cov_matrix << std::endl;
	      double cov = it_prev->getMeasToTrackMatchingCov();
// 	  ROS_INFO("a8");
	      
	      if (cov == 0) { ROS_ERROR("assign_munkres: cov = 0"); continue; }
// 	  ROS_INFO("a9");
	      double mahalanobis_dist = std::sqrt((std::pow((p.x - it_prev->getPos().x), 2) +
						    std::pow((p.y - it_prev->getPos().y), 2)) / cov);
// 	  ROS_INFO("a10");
	      double dist = distanceBtwTwoPoints(p, it_prev->getPos());
// 	  ROS_INFO("a11");
	      
	      cov_ellipse_ma.markers.push_back(getCovarianceEllipse(getNextCovEllipseId(), it_prev->getPos().x,
		it_prev->getPos().y, cov_matrix));
// 	  ROS_INFO("a12");
	      
// 	      ROS_INFO("\n\nmahalanobis_dist:\n\n");
// 	      ROS_INFO("Prev pos: (%f, %f)", it_prev->getPos().x, it_prev->getPos().y);
// 	      ROS_INFO("Meas pos: (%f, %f)", p.x, p.y);
// 	      ROS_INFO("cov: %f, maha: %f, dist: %f", cov, mahalanobis_dist, dist);
	      
// 	      double vel = calculateNorm(it_prev->getVel());
// 	  ROS_INFO("a13");
	      
	      if (dist <= 0.03)
	      {
		matrix(r, c) = 0;
	      } 
	      else if (mahalanobis_dist < mahalanobis_dist_gate && dist < 0.5 * max_dist_btw_legs) 
	      {
		matrix(r, c) = mahalanobis_dist;
	      } 
	      else 
	      {
		matrix(r, c) = max_cost;
	      }
// 	  ROS_INFO("a14");
	    // }
	  }
	  r++;
      }
// 	  ROS_INFO("a15");
      
      cov_marker_pub.publish(cov_ellipse_ma);
// 	  ROS_INFO("a16");
      
//       std::string s = "\n\nmatrix before:\n";
//       for (int i = 0; i < rows; i++) {
// 	for (int j = 0; j < cols; j++) {
// 	  s.append(std::to_string(matrix(i, j)));
// 	  s.append("__");
// 	}
// 	s.append("\n");
//       }
//       ROS_INFO("%s", s.c_str());
// 
      Munkres<double> m;
      m.solve(matrix);
// 	  ROS_INFO("a17");
//       
//       s = "\n\nmatrix after:\n";
//       for (int i = 0; i < rows; i++) {
// 	for (int j = 0; j < cols; j++) {
// 	  s.append(std::to_string(matrix(i, j)));
// 	  s.append("__");
// 	}
// 	s.append("\n");
//       }
//       ROS_INFO("%s", s.c_str());

      // Use the assignment to update the old tracks with new blob measurement
      int meas_it = 0;
      for(r = 0; r < rows; r++) {
	std::vector<Leg>::iterator it_prev = tracks.begin();
	for (int c = 0; c < cols; c++) {
	    if (matrix(r,c) == 0) {
		if (r < meas_count && c < tracks_count) {
		      // Does the measurement fall within 3 std's of
		      // the track?
			    
		  double cov = it_prev->getMeasToTrackMatchingCov();
		  double mahalanobis_dist = std::sqrt((std::pow((
		    meas.points[meas_it].x - it_prev->getPos().x), 2) +
		    std::pow((meas.points[meas_it].y - it_prev->getPos().y), 2)) / cov);
		  double dist = distanceBtwTwoPoints(meas.points[meas_it], it_prev->getPos());
    // 			   if (it_prev->is_within_region(meas.points[meas_it],3)) {
		  
		  if (mahalanobis_dist < mahalanobis_dist_gate &&
		    dist < 0.48 * max_dist_btw_legs) 
		  {
		      // Found an assignment. Update the new measurement
		      // with the track ID and age of older track. Add
		      // to fused list
		      //it->matched_track(*it_prev);
		      it_prev->update(meas.points[meas_it]);
		      fused.push_back(*it_prev);
		  } else {
		      // TOO MUCH OF A JUMP IN POSITION
		      // Probably a missed track or a new track
		      it_prev->missed();
		      fused.push_back(*it_prev);

		      // And a new track
		      fused.push_back(initLeg(meas.points[meas_it]));
		  }
		} else if (r >= meas_count) {
		      it_prev->missed();
		      fused.push_back(*it_prev);
		} else if (c >= tracks_count) {
		      // Possible new track
		      fused.push_back(initLeg(meas.points[meas_it]));
		}
		break; // There is only one assignment per row
	  }
	  if (c < tracks_count-1) {
	      it_prev++;
	  }
	}
	if (r < meas_count-1) {
	      meas_it++;
	}
      }
  }

  unsigned int getNextLegId()
  {
    return next_leg_id++;
  }

  void pub_circle_with_id(double x, double y, int id)
  {
    visualization_msgs::Marker marker;
    marker.header.frame_id = transform_link;
    marker.header.stamp = ros::Time();
    marker.ns = nh_.getNamespace();
    marker.id = id;
    marker.type = visualization_msgs::Marker::CYLINDER;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.x = x;
    marker.pose.position.y = y;
    marker.pose.position.z = z_coordinate;
//     marker.pose.orientation.x = 0.0;
//     marker.pose.orientation.y = 0.0;
//     marker.pose.orientation.z = 0.0;
//     marker.pose.orientation.w = 1.0;
    marker.scale.x = leg_radius;
    marker.scale.y = leg_radius;
//     marker.scale.z = 0;
    marker.color.a = 1.0; // Don't forget to set the alpha!
    if (id == 0)
      marker.color.r = 1.0;
    if (id == 2)
      marker.color.g = 1.0;
    if (id == 1)
      marker.color.b = 1.0;
    //only if using a MESH_RESOURCE marker type:
//     marker.mesh_resource = "package://pr2_description/meshes/base_v0/base.dae";
    vis_pub.publish( marker );

  }

  void pub_line(double x, double y, double width)
  {
    visualization_msgs::Marker marker;
    marker.header.frame_id = transform_link;
    marker.header.stamp = ros::Time();
    marker.ns = nh_.getNamespace();
//     marker.id = id;
    marker.type = visualization_msgs::Marker::LINE_STRIP;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.x = x;
    marker.pose.position.y = y;
    marker.pose.position.z = 0.0;
//     marker.pose.orientation.x = orientation_x;
//     marker.pose.orientation.y = orientation_y;
//     marker.pose.orientation.z = orientation_z;
//     marker.pose.orientation.w = orientation_w;
    marker.scale.x = width;
//     marker.scale.y = y_upper_limit - y_lower_limit;
//     marker.scale.z = 0;
    marker.color.a = 1.0; // Don't forget to set the alpha!
    marker.color.g = 1.0;
//     if (id == 0)
//       marker.color.r = 1.0;
//     if (id == 2)
//       marker.color.g = 1.0;
//     if (id == 1)
//       marker.color.b = 1.0;
    //only if using a MESH_RESOURCE marker type:
//     marker.mesh_resource = "package://pr2_description/meshes/base_v0/base.dae";
    vis_pub.publish( marker );
  }

  void pub_border_square()
  {
    visualization_msgs::Marker marker;
    marker.header.frame_id = transform_link;
    marker.header.stamp = ros::Time();
    marker.ns = nh_.getNamespace();
//     marker.id = id;
    marker.type = visualization_msgs::Marker::CUBE;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.x = (x_upper_limit + x_lower_limit) / 2;
    marker.pose.position.y = (y_upper_limit + y_lower_limit) / 2;
    marker.pose.position.z = 0.0;
//     marker.pose.orientation.x = orientation_x;
//     marker.pose.orientation.y = orientation_y;
//     marker.pose.orientation.z = orientation_z;
//     marker.pose.orientation.w = orientation_w;
    marker.scale.x = x_upper_limit - x_lower_limit;
    marker.scale.y = y_upper_limit - y_lower_limit;
//     marker.scale.z = 0;
    marker.color.a = 1.0; // Don't forget to set the alpha!
    marker.color.g = 1.0;
//     if (id == 0)
//       marker.color.r = 1.0;
//     if (id == 2)
//       marker.color.g = 1.0;
//     if (id == 1)
//       marker.color.b = 1.0;
    //only if using a MESH_RESOURCE marker type:
//     marker.mesh_resource = "package://pr2_description/meshes/base_v0/base.dae";
    tracking_area_pub.publish( marker );
  }

//   void pub_oval(double x, double y, double scale_x, double scale_y, double orientation_x,
//     double orientation_y, double orientation_z, double orientation_w, int id)
//   {
//     visualization_msgs::Marker marker;
//     marker.header.frame_id = transform_link;
//     marker.header.stamp = ros::Time();
//     marker.ns = nh_.getNamespace();
//     if (id == -2) { marker.id = 0; }
//     marker.id = id;
//     marker.type = visualization_msgs::Marker::CYLINDER;
//     marker.action = visualization_msgs::Marker::ADD;
//     marker.pose.position.x = x;
//     marker.pose.position.y = y;
//     marker.pose.position.z = z_coordinate;
//     marker.pose.orientation.x = orientation_x;
//     marker.pose.orientation.y = orientation_y;
//     marker.pose.orientation.z = orientation_z;
//     marker.pose.orientation.w = orientation_w;
//     marker.scale.x = scale_x;
//     marker.scale.y = scale_y;
// //     marker.scale.z = 0;
//     marker.color.a = 1.0; // Don't forget to set the alpha!
// //     if (id == 0)
// //       marker.color.r = 1.0;
// //     if (id == 2)
// //       marker.color.g = 1.0;
//     if (id == -2) { marker.color.b = 1.0; }
// //     if (id == 1)
// //       marker.color.b = 1.0;
//     //only if using a MESH_RESOURCE marker type:
// //     marker.mesh_resource = "package://pr2_description/meshes/base_v0/base.dae";
//     vis_pub.publish( marker );
//   }


  void pub_circle_with_radius(double x, double y, double radius)
  {
    visualization_msgs::Marker marker;
    marker.header.frame_id = transform_link;
    marker.header.stamp = ros::Time();
    marker.ns = nh_.getNamespace();
    marker.id = 0;
    marker.type = visualization_msgs::Marker::CYLINDER;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.x = x;
    marker.pose.position.y = y;
    marker.pose.position.z = z_coordinate / 2;
    marker.scale.x = radius;
    marker.scale.y = radius;
    marker.scale.z = z_coordinate;
    marker.color.a = 1.0; // Don't forget to set the alpha!
    vis_pub.publish( marker );
  }

  void pub_circle(double x, double y, double radius, bool isLeft, int id)
  {
    visualization_msgs::Marker marker;
    marker.header.frame_id = transform_link;
    marker.header.stamp = ros::Time();
    marker.ns = nh_.getNamespace();
    marker.id = id;
    marker.type = visualization_msgs::Marker::CYLINDER;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.x = x;
    marker.pose.position.y = y;
    marker.pose.position.z = z_coordinate;
//     marker.pose.orientation.x = 0.0;
//     marker.pose.orientation.y = 0.0;
//     marker.pose.orientation.z = 0.0;
//     marker.pose.orientation.w = 1.0;
    marker.scale.x = radius;
    marker.scale.y = radius;
//     marker.scale.z = 0;
    marker.color.a = 1.0; // Don't forget to set the alpha!
    if (isLeft)
    {
      marker.color.r = 1.0;
      if (id == 2) { marker.color.b = 1.0; }
//       marker.color.g = 0.0;
    }
    else
    {
//       marker.color.r = 0.0;
      marker.color.g = 1.0;
      if (id == 3) { marker.color.b = 1.0; }
    }
//     marker.color.b = 0.0;
    //only if using a MESH_RESOURCE marker type:
//     marker.mesh_resource = "package://pr2_description/meshes/base_v0/base.dae";
    vis_pub.publish( marker );

  }

//   void checkPeopleId()
//   {
//     for (Leg& l : legs) {
//       if (!l.hasPair()) { continue; }
//
//     }
//   }


  void processLaserScan(const sensor_msgs::LaserScan::ConstPtr& scan)
  {
    if (!got_map) { return; }
    pub_border_square();
//     pub_line((x_upper_limit - x_lower_limit) / 2, y_lower_limit);
//     pub_line((x_upper_limit - x_lower_limit) / 2, y_upper_limit);
//     pub_line(x_lower_limit, (y_upper_limit - y_lower_limit) / 2);
//     pub_line(x_upper_limit, (y_upper_limit - y_lower_limit) / 2);
    sensor_msgs::PointCloud2 cloudFromScan, tfTransformedCloud;
//     ROS_INFO("1");

    if (!laserScanToPointCloud2(scan, cloudFromScan)) { predictLegs(); return; }
//     ROS_INFO("2");

    if (!tfTransformOfPointCloud2(scan, cloudFromScan, tfTransformedCloud)) { predictLegs(); return; }
//     ROS_INFO("3");

    sensor_msgs_point_cloud_publisher.publish(tfTransformedCloud);
//     ROS_INFO("4");

    pcl::PCLPointCloud2::Ptr pcl_pc2 (new pcl::PCLPointCloud2());
//     ROS_INFO("5");

    pcl_conversions::toPCL(tfTransformedCloud, *pcl_pc2);
//     ROS_INFO("6");

    PointCloud cloudXYZ, filteredCloudXYZ;
//     ROS_INFO("7");
    pcl::fromPCLPointCloud2(*pcl_pc2, cloudXYZ);
//     ROS_INFO("8");
    if (!filterPCLPointCloud(cloudXYZ, filteredCloudXYZ)) { predictLegs(); return; }
//     ROS_INFO("9");

    PointCloud cluster_centroids;
//     ROS_INFO("10");

    // clustering
    if (!clustering(filteredCloudXYZ, cluster_centroids)) { predictLegs(); return; }
//     ROS_INFO("11");

    ROS_WARN("\nvor: \n");
    printLegsInfo(legs, "legs");
    if (isOnePersonToTrack) {
      matchLegCandidates(cluster_centroids);
    } else {
      gnn_munkres(cluster_centroids);
    }
    ROS_WARN("\nnach: \n");
    printLegsInfo(legs, "legs");
    visLegs();
//     ROS_INFO("12");
//     checkPeopleId();
    PointCloud new_people;
//     ROS_INFO("13");
    std::vector<int> new_people_idx;
//     ROS_INFO("14");
    findPeople(new_people, new_people_idx);
//     ROS_INFO("15");

//     gnn_munkres_people(new_people, new_people_idx);

    vis_people();
//     ROS_INFO("16");
//     vis_persons();


//     GNN(cluster_centroids);
//     pcl_cloud_publisher.publish(filteredCloudXYZ.makeShared());

//     PointCloud::Ptr left(new PointCloud());
//     left->header = filteredCloudXYZ.header;
//
//     PointCloud::Ptr right(new PointCloud());
//     right->header = filteredCloudXYZ.header;
//
//     sortPointCloudToLeftAndRight(filteredCloudXYZ, left, right);
//
// //     pubCircularityAndLinearity(left, true);
// //     pubCircularityAndLinearity(right, false);
//
//     if (!useKalmanFilterAndPubCircles(left, true)) { return; }
//     if (!useKalmanFilterAndPubCircles(right, false)) { return; }


  }
};


//   filters::MultiChannelFilterBase<double>* f;

int main(int argc, char **argv)
{
  ros::init(argc, argv,"leg_tracker");
  ros::NodeHandle nh("~");
  LegDetector ld(nh);
  ros::spin();


//     f = new iirob_filters::MultiChannelKalmanFilter<double>();
//     f->configure();
//
//     for (int i = 0; i < 100; ++i)
//     {
//
//     std::vector<double> v, out;
//     v.push_back(i);
//     v.push_back(i + 1);
//
//     ROS_INFO("IN: %f, %f", v[0], v[1]);
//
//
//     f->update(v, out);
//
//     ROS_INFO("OUT: %f, %f", out[0], out[1]);
//
//
//     }
  return 0;
}
