// #include <fstream>
#include <plan_manage/planner_manager.h>
#include <thread>

namespace ego_planner
{

  // SECTION interfaces for setup and query

  EGOPlannerManager::EGOPlannerManager() {}

  EGOPlannerManager::~EGOPlannerManager() { std::cout << "des manager" << std::endl; }
void EGOPlannerManager::initPlanModules(ros::NodeHandle &nh, PlanningVisualization::Ptr vis)
{
    /* 读取算法参数 */
    
    // 从参数服务器中读取最大速度参数，如果没有设置，则默认值为-1.0
    nh.param("manager/max_vel", pp_.max_vel_, -1.0);
    // 从参数服务器中读取最大加速度参数，如果没有设置，则默认值为-1.0
    nh.param("manager/max_acc", pp_.max_acc_, -1.0);
    // 从参数服务器中读取最大跃度（jerk）参数，如果没有设置，则默认值为-1.0
    nh.param("manager/max_jerk", pp_.max_jerk_, -1.0);
    // 从参数服务器中读取可行性容差参数，如果没有设置，则默认值为0.0
    nh.param("manager/feasibility_tolerance", pp_.feasibility_tolerance_, 0.0);
    // 从参数服务器中读取控制点距离参数，如果没有设置，则默认值为-1.0
    nh.param("manager/control_points_distance", pp_.ctrl_pt_dist, -1.0);
    // 从参数服务器中读取规划地平线参数，如果没有设置，则默认值为5.0
    nh.param("manager/planning_horizon", pp_.planning_horizen_, 5.0);

    // 初始化本地数据的轨迹ID为0
    local_data_.traj_id_ = 0;
    // 重置栅格地图对象，并初始化
    grid_map_.reset(new GridMap);
    grid_map_->initMap(nh);

    // 重置B样条优化器，并设置参数和环境
    bspline_optimizer_rebound_.reset(new BsplineOptimizer);
    bspline_optimizer_rebound_->setParam(nh);
    bspline_optimizer_rebound_->setEnvironment(grid_map_);
    // 初始化A星算法并设置栅格地图
    bspline_optimizer_rebound_->a_star_.reset(new AStar);
    bspline_optimizer_rebound_->a_star_->initGridMap(grid_map_, Eigen::Vector3i(100, 100, 100));

    // 设置可视化对象
    visualization_ = vis;
}

  // !SECTION

  // SECTION rebond replanning

  bool EGOPlannerManager::reboundReplan(Eigen::Vector3d start_pt, Eigen::Vector3d start_vel,
                                        Eigen::Vector3d start_acc, Eigen::Vector3d local_target_pt,
                                        Eigen::Vector3d local_target_vel, bool flag_polyInit, bool flag_randomPolyTraj)
  {

    // 初始flag_polyInit为true；flag_randomPolyTra为false；
    static int count = 0;
    // 打印重规划计数和相关信息
    std::cout << endl
              << "[rebo replan]: -------------------------------------" << count++ << std::endl;
    cout.precision(3);// 设置浮点数精度
    cout << "start: " << start_pt.transpose() << ", " << start_vel.transpose() << "\ngoal:" << local_target_pt.transpose() << ", " << local_target_vel.transpose()
         << endl;
  // 如果起点和终点非常接近，则增加连续失败次数并返回false
  //起点和给定的局部终点台接近
    if ((start_pt - local_target_pt).norm() < 0.2)
    {
      cout << "起点和给定的局部终点台接近" << endl;
      cout << "Close to local_target_pt goal" << endl;
      continous_failures_count_++;
      return false;
    }
  // 开始计时
    ros::Time t_start = ros::Time::now();
    ros::Duration t_init, t_opt, t_refine;

    /*** STEP 1: INIT ***/

      // 初始化步骤：计算开始和结束点的距离，决定轨迹初始点集的时间间隔
    double ts = (start_pt - local_target_pt).norm() > 0.1 ? pp_.ctrl_pt_dist / pp_.max_vel_ * 1.2 
    : pp_.ctrl_pt_dist / pp_.max_vel_ * 5; 
    //这里不懂

    // 计算时间间隔ts。如果起点和目标点之间的距离大于0.1，则ts为控制点之间距离与最大速度比的1.2倍；否则，ts为此比值的5倍。
    // 这是为了防止在紧密的路径规划中超出加速度或速度限制。

    vector<Eigen::Vector3d> point_set, start_end_derivatives; // 声明两个向量：point_set用于存储路径上的点，start_end_derivatives用于存储路径起始和结束点的导数信息（例如速度和加速度）。

    static bool flag_first_call = true, flag_force_polynomial = false;// 声明并初始化两个静态布尔变量。flag_first_call表示是否是函数的第一次调用；flag_force_polynomial表示是否强制使用多项式轨迹。

    bool flag_regenerate = false;// 声明一个布尔变量flag_regenerate，用于指示是否需要重新生成路径。初始化为false。

    do
    {
      point_set.clear();
      start_end_derivatives.clear();
      flag_regenerate = false;
       
        // flag_polyInit 为true
      if (flag_first_call || flag_polyInit || flag_force_polynomial /*|| ( start_pt - local_target_pt ).norm() < 1.0*/) // Initial path generated from a min-snap traj by order.
      {
        flag_first_call = false;
        flag_force_polynomial = false;

        PolynomialTraj gl_traj;

            // 计算起始点到目标点的直线距离
        double dist = (start_pt - local_target_pt).norm();

        // 根据最大速度和加速度计算从起始点到目标点的预估时间
        double time = pow(pp_.max_vel_, 2) / pp_.max_acc_ > dist ? 
            // 如果可以在不超过最大加速度的情况下达到最大速度
            sqrt(dist / pp_.max_acc_) : 
            // 如果不能在给定距离内达到最大速度，则计算加速到最高速度所需时间，以最高速度行驶一段距离，然后减速到停止所需的总时间
            (dist - pow(pp_.max_vel_, 2) / pp_.max_acc_) / pp_.max_vel_ + 2 * pp_.max_vel_ / pp_.max_acc_;
        /*flag_randomPolyTraj 第一次调用为false，后面为true*/
        if (!flag_randomPolyTraj)/*第一调用只有两个点？*/
        {
          gl_traj = PolynomialTraj::one_segment_traj_gen(start_pt, start_vel, start_acc, local_target_pt, local_target_vel, Eigen::Vector3d::Zero(), time);
        }
        else
        {
         // 计算水平方向向量，为起始点和目标点向量差的叉积与Z轴向量(0,0,1)的规范化结果
            Eigen::Vector3d horizen_dir = ((start_pt - local_target_pt).cross(Eigen::Vector3d(0, 0, 1))).normalized();

            // 计算垂直方向向量，为起始点和目标点向量差与水平方向向量的叉积的规范化结果
            Eigen::Vector3d vertical_dir = ((start_pt - local_target_pt).cross(horizen_dir)).normalized();

            // 生成一个随机插入点
            Eigen::Vector3d random_inserted_pt = (start_pt + local_target_pt) / 2 +
                                                // 在水平方向上添加一个随机偏移
                                                (((double)rand()) / RAND_MAX - 0.5) * (start_pt - local_target_pt).norm() * horizen_dir * 0.8 * (-0.978 / (continous_failures_count_ + 0.989) + 0.989) +
                                                // 在垂直方向上添加一个随机偏移
                                                (((double)rand()) / RAND_MAX - 0.5) * (start_pt - local_target_pt).norm() * vertical_dir * 0.4 * (-0.978 / (continous_failures_count_ + 0.989) + 0.989);

            // 初始化位置矩阵
            Eigen::MatrixXd pos(3, 3);
            // 设置起始点、随机插入点和目标点
            pos.col(0) = start_pt;
            pos.col(1) = random_inserted_pt;
            pos.col(2) = local_target_pt;

            // 初始化时间向量
            Eigen::VectorXd t(2);
            // 设置时间间隔
            t(0) = t(1) = time / 2;

            // 使用位置和时间信息生成多项式轨迹
            gl_traj = PolynomialTraj::minSnapTraj(pos, start_vel, local_target_vel, start_acc, Eigen::Vector3d::Zero(), t);

        }

       double t;
bool flag_too_far;
ts *= 1.5; // 将时间间隔ts增大1.5倍，此操作是为了接下来的循环中逐步减小ts

do
{
    ts /= 1.5; // 逐步减小时间间隔ts
    point_set.clear(); // 清空之前的点集
    flag_too_far = false; // 初始化标志，表示点之间的距离是否过大
    Eigen::Vector3d last_pt = gl_traj.evaluate(0); // 计算轨迹上的起始点

    // 遍历轨迹，生成点集
    for (t = 0; t < time; t += ts)
    {
        Eigen::Vector3d pt = gl_traj.evaluate(t); // 计算当前时刻的轨迹点
        // 检查当前点与上一个点之间的距离是否过大
        if ((last_pt - pt).norm() > pp_.ctrl_pt_dist * 1.5)
        {
            flag_too_far = true; // 如果距离过大，设置标志为真
            break; // 跳出循环，重新调整时间间隔
        }
        last_pt = pt; // 更新上一个点
        point_set.push_back(pt); // 将当前点添加到点集中
    }
} while (flag_too_far || point_set.size() < 7); // 如果点之间距离过大或点的数量不足7个，重新执行循环

      t -= ts; // 调整时间变量t，以便计算最后一个轨迹点的导数信息

      // 将轨迹起点和终点的速度和加速度信息添加到导数集合中
      start_end_derivatives.push_back(gl_traj.evaluateVel(0)); // 起点的速度
      start_end_derivatives.push_back(local_target_vel); // 终点的速度
      start_end_derivatives.push_back(gl_traj.evaluateAcc(0)); // 起点的加速度
      start_end_derivatives.push_back(gl_traj.evaluateAcc(t)); // 终点的加速度

      }
     else // Initial path generated from previous trajectory. 路径是由上一次的轨迹生成
      {
        //local_data；在那里赋值？？
          double t;
          double t_cur = (ros::Time::now() - local_data_.start_time_).toSec(); // 获取当前时间距离轨迹起始时间的秒数

          vector<double> pseudo_arc_length; // 伪弧长向量
          vector<Eigen::Vector3d> segment_point; // 分段点向量
          pseudo_arc_length.push_back(0.0); // 将0.0（起点）推入伪弧长向量

          for (t = t_cur; t < local_data_.duration_ + 1e-3; t += ts) // 循环遍历时间段，步长为ts
          {
              segment_point.push_back(local_data_.position_traj_.evaluateDeBoorT(t)); // 计算并推入当前时间点的轨迹位置
              if (t > t_cur)
              {
                  // 计算当前点与前一点的距离，并累加到伪弧长向量中
                  pseudo_arc_length.push_back((segment_point.back() - segment_point[segment_point.size() - 2]).norm() + pseudo_arc_length.back());
              }
          }
          t -= ts; // 将时间回退一个步长

          // 计算从当前轨迹点到目标点所需的时间，考虑最大速度
          double poly_time = (local_data_.position_traj_.evaluateDeBoorT(t) - local_target_pt).norm() / pp_.max_vel_ * 2;
          if (poly_time > ts)
          {
              // 生成从当前轨迹点到目标点的多项式轨迹
              PolynomialTraj gl_traj = PolynomialTraj::one_segment_traj_gen(local_data_.position_traj_.evaluateDeBoorT(t),
                                                                            local_data_.velocity_traj_.evaluateDeBoorT(t),
                                                                            local_data_.acceleration_traj_.evaluateDeBoorT(t),
                                                                            local_target_pt, local_target_vel, Eigen::Vector3d::Zero(), poly_time);

              for (t = ts; t < poly_time; t += ts)
              {
                  if (!pseudo_arc_length.empty())
                  {
                      // 计算多项式轨迹的下一段并将其推入segment_point向量中，同时更新伪弧长
                      segment_point.push_back(gl_traj.evaluate(t));
                      pseudo_arc_length.push_back((segment_point.back() - segment_point[segment_point.size() - 2]).norm() + pseudo_arc_length.back());
                  }
                  else
                  {
                      ROS_ERROR("pseudo_arc_length is empty, return!"); // 错误处理：如果伪弧长向量为空，返回错误
                      continous_failures_count_++; // 连续失败计数器加一
                      return false;
                  }
              }
          }

          double sample_length = 0; // 采样长度初始化为0
          double cps_dist = pp_.ctrl_pt_dist * 1.5; // 控制点距离乘以1.5
          size_t id = 0;

          do
          {
              cps_dist /= 1.5; // 控制点距离每次循环除以1.5
              point_set.clear(); // 清空点集
              sample_length = 0;
              id = 0;
              while ((id <= pseudo_arc_length.size() - 2) && sample_length <= pseudo_arc_length.back())
              {
                  if (sample_length >= pseudo_arc_length[id] && sample_length < pseudo_arc_length[id + 1])
                  {
                      // 根据采样长度插值计算当前控制点的位置并推入点集
                      point_set.push_back((sample_length - pseudo_arc_length[id]) / (pseudo_arc_length[id + 1] - pseudo_arc_length[id]) * segment_point[id + 1] +
                                          (pseudo_arc_length[id + 1] - sample_length) / (pseudo_arc_length[id + 1] - pseudo_arc_length[id]) * segment_point[id]);
                      sample_length += cps_dist; // 增加采样长度
                  }
                  else
                      id++;
              }
              point_set.push_back(local_target_pt); // 将目标点加入点集
          } while (point_set.size() < 7); // 如果点集大小小于7，继续减少控制点距离并重新采样

          start_end_derivatives.push_back(local_data_.velocity_traj_.evaluateDeBoorT(t_cur)); // 起始速度导数
          start_end_derivatives.push_back(local_target_vel); // 目标速度导数
          start_end_derivatives.push_back(local_data_.acceleration_traj_.evaluateDeBoorT(t_cur)); // 起始加速度导数
          start_end_derivatives.push_back(Eigen::Vector3d::Zero()); // 终止加速度导数为零

          if (point_set.size() > pp_.planning_horizen_ / pp_.ctrl_pt_dist * 3) // 如果初始路径长度异常
          {
              flag_force_polynomial = true; // 强制多项式标志设为真
              flag_regenerate = true; // 重新生成标志设为真
          }
      }
    } while (flag_regenerate);

          // 参数化为B-spline轨迹
        Eigen::MatrixXd ctrl_pts;
        UniformBspline::parameterizeToBspline(ts, point_set, start_end_derivatives, ctrl_pts);

        // 初始化A*路径优化
        vector<vector<Eigen::Vector3d>> a_star_pathes;
        a_star_pathes = bspline_optimizer_rebound_->initControlPoints(ctrl_pts, true);

        // 记录初始化阶段所用时间
        t_init = ros::Time::now() - t_start;

        // 可视化初始路径和A*路径
        static int vis_id = 0;
        visualization_->displayInitPathList(point_set, 0.2, 0);
        visualization_->displayAStarList(a_star_pathes, vis_id);

        // 开始优化阶段的计时
        t_start = ros::Time::now();

        /*** STEP 2: OPTIMIZE ***/
        // 优化B-spline轨迹
        bool flag_step_1_success = bspline_optimizer_rebound_->BsplineOptimizeTrajRebound(ctrl_pts, ts);
        cout << "first_optimize_step_success=" << flag_step_1_success << endl;
        if (!flag_step_1_success)
        {
            // 如果优化失败，增加失败计数并返回false
            continous_failures_count_++;
            return false;
        }

        // 记录优化所用时间
        t_opt = ros::Time::now() - t_start;
        t_start = ros::Time::now();

        /*** STEP 3: REFINE(RE-ALLOCATE TIME) IF NECESSARY ***/
        // 如果必要，细化（重新分配时间）B-spline轨迹
        UniformBspline pos = UniformBspline(ctrl_pts, 3, ts);
        pos.setPhysicalLimits(pp_.max_vel_, pp_.max_acc_, pp_.feasibility_tolerance_);

        double ratio;
        bool flag_step_2_success = true;
        if (!pos.checkFeasibility(ratio, false))
        {
            cout << "Need to reallocate time." << endl;

            // 尝试重新分配时间以满足物理限制
            Eigen::MatrixXd optimal_control_points;
            flag_step_2_success = refineTrajAlgo(pos, start_end_derivatives, ratio, ts, optimal_control_points);
            if (flag_step_2_success)
                pos = UniformBspline(optimal_control_points, 3, ts);
        }

        // 如果细化失败，打印错误信息并增加失败计数
        if (!flag_step_2_success)
        {
            printf("\033[34mThis refined trajectory hits obstacles. It doesn't matter if appeares occasionally. But if continously appearing, Increase parameter \"lambda_fitness\".\n\033[0m");
            continous_failures_count_++;
            return false;
        }

        // 记录细化所用时间
        t_refine = ros::Time::now() - t_start;

        // 保存规划结果
        updateTrajInfo(pos, ros::Time::now());

        // 打印总耗时、优化耗时和细化耗时
        cout << "total time:\033[42m" << (t_init + t_opt + t_refine).toSec() << "\033[0m,optimize:" << (t_init + t_opt).toSec() << ",refine:" << t_refine.toSec() << endl;

        // 成功完成规划，重置连续失败次数并返回true
        continous_failures_count_ = 0;
        return true;

  }

  bool EGOPlannerManager::EmergencyStop(Eigen::Vector3d stop_pos)
  {
    Eigen::MatrixXd control_points(3, 6);
    for (int i = 0; i < 6; i++)
    {
      control_points.col(i) = stop_pos;
    }

    updateTrajInfo(UniformBspline(control_points, 3, 1.0), ros::Time::now());

    return true;
  }

  bool EGOPlannerManager::planGlobalTrajWaypoints(const Eigen::Vector3d &start_pos, const Eigen::Vector3d &start_vel, const Eigen::Vector3d &start_acc,
                                                  const std::vector<Eigen::Vector3d> &waypoints, const Eigen::Vector3d &end_vel, const Eigen::Vector3d &end_acc)
  {

    // generate global reference trajectory

    vector<Eigen::Vector3d> points;
    points.push_back(start_pos);

    for (size_t wp_i = 0; wp_i < waypoints.size(); wp_i++)
    {
      points.push_back(waypoints[wp_i]);
    }

    double total_len = 0;
    total_len += (start_pos - waypoints[0]).norm();
    for (size_t i = 0; i < waypoints.size() - 1; i++)
    {
      total_len += (waypoints[i + 1] - waypoints[i]).norm();
    }

    // insert intermediate points if too far
    vector<Eigen::Vector3d> inter_points;
    double dist_thresh = max(total_len / 8, 4.0);

    for (size_t i = 0; i < points.size() - 1; ++i)
    {
      inter_points.push_back(points.at(i));
      double dist = (points.at(i + 1) - points.at(i)).norm();

      if (dist > dist_thresh)
      {
        int id_num = floor(dist / dist_thresh) + 1;

        for (int j = 1; j < id_num; ++j)
        {
          Eigen::Vector3d inter_pt =
              points.at(i) * (1.0 - double(j) / id_num) + points.at(i + 1) * double(j) / id_num;
          inter_points.push_back(inter_pt);
        }
      }
    }

    inter_points.push_back(points.back());

    // for ( int i=0; i<inter_points.size(); i++ )
    // {
    //   cout << inter_points[i].transpose() << endl;
    // }

    // write position matrix
    int pt_num = inter_points.size();
    Eigen::MatrixXd pos(3, pt_num);
    for (int i = 0; i < pt_num; ++i)
      pos.col(i) = inter_points[i];

    Eigen::Vector3d zero(0, 0, 0);
    Eigen::VectorXd time(pt_num - 1);
    for (int i = 0; i < pt_num - 1; ++i)
    {
      time(i) = (pos.col(i + 1) - pos.col(i)).norm() / (pp_.max_vel_);
    }

    time(0) *= 2.0;
    time(time.rows() - 1) *= 2.0;

    PolynomialTraj gl_traj;
    if (pos.cols() >= 3)
      gl_traj = PolynomialTraj::minSnapTraj(pos, start_vel, end_vel, start_acc, end_acc, time);
    else if (pos.cols() == 2)
      gl_traj = PolynomialTraj::one_segment_traj_gen(start_pos, start_vel, start_acc, pos.col(1), end_vel, end_acc, time(0));
    else
      return false;

    auto time_now = ros::Time::now();
    global_data_.setGlobalTraj(gl_traj, time_now);

    return true;
  }
//第一步生成全局参考轨迹
// bool EGOPlannerManager::planGlobalTraj(const Eigen::Vector3d &start_pos, const Eigen::Vector3d &start_vel, const Eigen::Vector3d &start_acc,
//                                        const Eigen::Vector3d &end_pos, const Eigen::Vector3d &end_vel, const Eigen::Vector3d &end_acc)
// {
//     // 生成全局参考轨迹
//     vector<Eigen::Vector3d> points;
//     points.push_back(start_pos); // 添加起始点
//     points.push_back(end_pos);   // 添加终点

//     // 如果两点间距离过远，则插入中间点
//     vector<Eigen::Vector3d> inter_points;
//     const double dist_thresh = 4.0; // 设定距离阈值

//     for (size_t i = 0; i < points.size() - 1; ++i)
//     {
//         inter_points.push_back(points.at(i));
//         double dist = (points.at(i + 1) - points.at(i)).norm(); // 计算两点间的距离

//         // 如果距离超过阈值
//         if (dist > dist_thresh)
//         {
//             int id_num = floor(dist / dist_thresh) + 1; // 计算需要插入的中间点数量

//             // 插入中间点
//             for (int j = 1; j < id_num; ++j)
//             {
//                 Eigen::Vector3d inter_pt =
//                     points.at(i) * (1.0 - double(j) / id_num) + points.at(i + 1) * double(j) / id_num;
//                 inter_points.push_back(inter_pt);
//             }
//         }
//     }

//     inter_points.push_back(points.back()); // 添加最后一个点

//     // 构建位置矩阵
//     int pt_num = inter_points.size();
//     Eigen::MatrixXd pos(3, pt_num);
//     for (int i = 0; i < pt_num; ++i)
//         pos.col(i) = inter_points[i];

//     Eigen::Vector3d zero(0, 0, 0);
//     Eigen::VectorXd time(pt_num - 1);
//     for (int i = 0; i < pt_num - 1; ++i)
//     {
//         time(i) = (pos.col(i + 1) - pos.col(i)).norm() / (pp_.max_vel_);
//     }

//     // 调整起始和结束时间
//     time(0) *= 2.0;
//     time(time.rows() - 1) *= 2.0;

//     // 生成多项式轨迹
//     PolynomialTraj gl_traj;
//     if (pos.cols() >= 3)
//         gl_traj = PolynomialTraj::minSnapTraj(pos, start_vel, end_vel, start_acc, end_acc, time);
//     else if (pos.cols() == 2)
//         gl_traj = PolynomialTraj::one_segment_traj_gen(start_pos, start_vel, start_acc, end_pos, end_vel, end_acc, time(0));
//     else
//         return false;

//     auto time_now = ros::Time::now();
//     global_data_.setGlobalTraj(gl_traj, time_now); // 设置全局轨迹

//     return true; // 返回成功
// }

bool EGOPlannerManager::planGlobalTraj(const Eigen::Vector3d &start_pos, const Eigen::Vector3d &start_vel, const Eigen::Vector3d &start_acc,
                                       const Eigen::Vector3d &end_pos, const Eigen::Vector3d &end_vel, const Eigen::Vector3d &end_acc)
{
    // 生成全局参考轨迹点
    std::vector<Eigen::Vector3d> points{start_pos, end_pos};
    std::vector<Eigen::Vector3d> inter_points;
    const double dist_thresh = 4.0; // 距离阈值

    for (size_t i = 0; i < points.size() - 1; ++i)
    {
        inter_points.push_back(points[i]);
        double dist = (points[i + 1] - points[i]).norm(); // 计算两点间的距离

        // 如果距离超过阈值，插入中间点
        if (dist > dist_thresh)
        {
            int id_num = static_cast<int>(std::floor(dist / dist_thresh)) + 1; // 需要插入的中间点数量
            for (int j = 1; j < id_num; ++j)
            {
                inter_points.push_back(points[i] * (1.0 - double(j) / id_num) + points[i + 1] * double(j) / id_num);
            }
        }
    }

    inter_points.push_back(points.back()); // 添加最后一个点

    // 构建位置矩阵
    int pt_num = inter_points.size();
    Eigen::MatrixXd pos(3, pt_num);
    for (int i = 0; i < pt_num; ++i)
        pos.col(i) = inter_points[i];

    Eigen::VectorXd time(pt_num - 1);
    for (int i = 0; i < pt_num - 1; ++i)
    {
        time(i) = (pos.col(i + 1) - pos.col(i)).norm() / pp_.max_vel_;
    }

    // 调整起始和结束时间
    time(0) *= 2.0;
    time(time.size() - 1) *= 2.0;

    // 生成多项式轨迹
    PolynomialTraj gl_traj;
    if (pos.cols() >= 3)
        gl_traj = PolynomialTraj::minSnapTraj(pos, start_vel, end_vel, start_acc, end_acc, time);
    else if (pos.cols() == 2)
        gl_traj = PolynomialTraj::one_segment_traj_gen(start_pos, start_vel, start_acc, end_pos, end_vel, end_acc, time(0));
    else
        return false;

    global_data_.setGlobalTraj(gl_traj, ros::Time::now()); // 设置全局轨迹

    return true;
}

  bool EGOPlannerManager::refineTrajAlgo(UniformBspline &traj, vector<Eigen::Vector3d> &start_end_derivative, double ratio, double &ts, Eigen::MatrixXd &optimal_control_points)
  {
    double t_inc;

    Eigen::MatrixXd ctrl_pts; // = traj.getControlPoint()

    // std::cout << "ratio: " << ratio << std::endl;
    reparamBspline(traj, start_end_derivative, ratio, ctrl_pts, ts, t_inc);

    traj = UniformBspline(ctrl_pts, 3, ts);

    double t_step = traj.getTimeSum() / (ctrl_pts.cols() - 3);
    bspline_optimizer_rebound_->ref_pts_.clear();
    for (double t = 0; t < traj.getTimeSum() + 1e-4; t += t_step)
      bspline_optimizer_rebound_->ref_pts_.push_back(traj.evaluateDeBoorT(t));

    bool success = bspline_optimizer_rebound_->BsplineOptimizeTrajRefine(ctrl_pts, ts, optimal_control_points);

    return success;
  }

  void EGOPlannerManager::updateTrajInfo(const UniformBspline &position_traj, const ros::Time time_now)
  {
    local_data_.start_time_ = time_now;
    local_data_.position_traj_ = position_traj;
    local_data_.velocity_traj_ = local_data_.position_traj_.getDerivative();
    local_data_.acceleration_traj_ = local_data_.velocity_traj_.getDerivative();
    local_data_.start_pos_ = local_data_.position_traj_.evaluateDeBoorT(0.0);
    local_data_.duration_ = local_data_.position_traj_.getTimeSum();
    local_data_.traj_id_ += 1;
  }

  void EGOPlannerManager::reparamBspline(UniformBspline &bspline, vector<Eigen::Vector3d> &start_end_derivative, double ratio,
                                         Eigen::MatrixXd &ctrl_pts, double &dt, double &time_inc)
  {
    double time_origin = bspline.getTimeSum();
    int seg_num = bspline.getControlPoint().cols() - 3;
    // double length = bspline.getLength(0.1);
    // int seg_num = ceil(length / pp_.ctrl_pt_dist);

    bspline.lengthenTime(ratio);
    double duration = bspline.getTimeSum();
    dt = duration / double(seg_num);
    time_inc = duration - time_origin;

    vector<Eigen::Vector3d> point_set;
    for (double time = 0.0; time <= duration + 1e-4; time += dt)
    {
      point_set.push_back(bspline.evaluateDeBoorT(time));
    }
    UniformBspline::parameterizeToBspline(dt, point_set, start_end_derivative, ctrl_pts);
  }

} // namespace ego_planner
