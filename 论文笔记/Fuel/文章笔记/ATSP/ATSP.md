## ATSP（不对称旅行商问题）相关知识详解

#### 定义

不对称旅行商问题（Asymmetric Traveling Salesman Problem，ATSP）是组合优化中的一个重要且著名的问题。它的定义如下：

**给定一个完全有向图 \( G = (V, A) \)，其中 \( V \) 是顶点集，\( A \) 是弧集，且弧具有非负成本。求在图 \( G \) 中找到一个最小成本回路，使得每个顶点恰好经过一次。**

### 相关知识详解

#### 1. 图论基础

- **图 \( G = (V, A) \)**：
  - \( V \) 表示顶点集（Vertices Set），包含所有需要访问的城市或节点。
  - \( A \) 表示弧集（Arcs Set），包含所有顶点之间的路径，且每条弧都有一个关联的成本（非负）。

- **有向图**：
  - 图中的弧是有方向的，即从顶点 \( u \) 到顶点 \( v \) 的路径与从顶点 \( v \) 到顶点 \( u \) 的路径是不同的，且成本也可能不同。

#### 2. 成本矩阵

- **成本矩阵**：
  - 成本矩阵是一个 \( |V| \times |V| \) 的矩阵 \( C \)，其中 \( C[i][j] \) 表示从顶点 \( i \) 到顶点 \( j \) 的成本。如果路径不存在，则成本可以设为一个很大的值。

#### 3. 优化目标

- **最小成本回路**：
  - 寻找一个从某个顶点开始，经过所有顶点且最终回到起点的路径，使得总成本最小。这个路径被称为哈密顿回路（Hamiltonian Circuit）。

#### 4. 组合优化与复杂度

- **NP难问题**：
  - ATSP 是一个 NP难问题，这意味着目前没有已知的多项式时间算法可以解决所有实例。问题的难度随着顶点数的增加而指数级增长。

- **近似算法与启发式算法**：
  - 由于问题的复杂性，通常使用近似算法或启发式算法来找到可接受的解，例如贪心算法、动态规划、遗传算法和蚁群算法等。

#### 5. 具体算法与求解方法

- **动态规划**：
  - 例如 Held-Karp 算法，它使用动态规划的思想，时间复杂度为 \( O(n^2 \cdot 2^n) \)。

- **启发式方法**：
  - 例如最近邻算法、遗传算法、模拟退火、蚁群优化等，这些方法不能保证找到最优解，但在实际应用中能快速找到接近最优的解。

### ATSP 在图中系统架构的作用

在系统架构图中，ATSP 求解用于全局探索规划中的路径优化。具体来说：

- **Frontier-Level Cost Calculate（前沿级别成本计算）**：
  - 计算不同前沿点的成本，并将其输入到 ATSP 求解模块。

- **ATSP Solving（不对称旅行商问题求解）**：
  - 使用 ATSP 求解优化前沿点的访问顺序，从而找到最优的探索路径。

- **结果应用**：
  - 最优路径用于更新飞行轨迹，使得无人机或机器人能够以最小成本高效地覆盖探索区域。

### 总结

不对称旅行商问题（ATSP）是一个经典的组合优化问题，涉及图论、优化算法和计算复杂度等多方面的知识。在自主飞行器的全局探索规划中，通过求解 ATSP，可以找到最优的前沿点访问顺序，从而实现高效的路径规划和探索任务。


在FUEL论文中，不对称旅行商问题（ATSP）被用来解决全局探索路径规划。以下是论文中如何建立ATSP问题及其原因的详细说明：

## FUEL论文中如何建立ATSP问题

1. **集群表示**：
   - 环境被划分为若干个前沿集群。每个集群代表一组与已探索区域相邻的未探索单元。这些集群作为潜在的探索目的地。

2. **成本矩阵的构建**：
   - 构建一个成本矩阵 \( M_{tsp} \)，用于表示集群之间的旅行成本。 \( M_{tsp} \) 的维度为 \( (N_{cls} + 1) \times (N_{cls} + 1) \)，其中 \( N_{cls} \) 是集群的数量。
   - 成本矩阵的第一行和第一列对应当前的视点 \( x_0 \)，剩下的 \( N_{cls} \times N_{cls} \) 块表示前沿集群对之间的连接成本。

3. **成本评估**：
   - 集群 \( k1 \) 和 \( k2 \) 之间的旅行成本 \( M_{tsp}(k1, k2) \) 计算为 \( t_{lb}(x_{k1,1}, x_{k2,1}) \)，其中 \( t_{lb} \) 是起始点之间的时间下限。
   - 从当前视点 \( x_0 \) 到第 \( k \) 个集群的成本为：
     \[
     M_{tsp}(0, k) = t_{lb}(x_0, x_{k,1}) + w_c \cdot c_c(x_{k,1})
     \]
     其中 \( w_c \) 是运动一致性成本 \( c_c \) 的权重，用于惩罚飞行方向的大幅变化。

4. **转换为ATSP**：
   - 为了将问题转换为ATSP，从其他集群到当前视点的连接成本设置为零：
     \[
     M_{tsp}(k, 0) = 0, \quad \forall k \in \{0, 1, 2, \cdots, N_{cls}\}
     \]
   - 这样可以确保返回当前视点不会增加额外成本，允许ATSP求解器通过求解一个闭环来找到最优的开环路径，然后进行转换。

### 原因

1. **高效的全局覆盖**：
   - ATSP建立使得规划器能够计算出一个高效覆盖所有前沿集群的全局路径，确保无人机能够以最小的旅行距离和时间访问所有重要区域。

2. **优化旅行路径**：
   - 通过引入时间下限和运动一致性成本，规划器可以生成一条最小化飞行时间且避免不必要往返的路径，导致更平滑和快速的探索。

3. **利用现有算法求解**：
   - ATSP可以通过现有的启发式算法如Lin-Kernighan-Helsgaun启发式来求解，这些算法高效且能快速提供近似最优解，使得该方法在实时应用中是可行的。

4. **处理非对称成本**：
   - 在无人机探索中，由于环境因素如风或障碍物，从一点到另一点的旅行成本可能是不对称的。ATSP能够有效处理这种不对称性，确保成本估计的准确性。

### 论文中的具体实现

FUEL论文中通过构建成本矩阵 \( M_{tsp} \) 并求解它来确定最优的探索路径。结果是一条高效覆盖所有前沿集群的路径，促进了对未知环境的快速彻底探索。这种方法显著提高了探索效率，实验结果显示，相较于其他先进的方法，完成时间更快【93:0†source】【93:2†source】【93:3†source】。