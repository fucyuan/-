## 贝塞尔曲线公式

贝塞尔曲线通过控制点 $((P_0, P_1, \ldots, P_n))$ 定义。对于参数 $(t)$（范围在 [0, 1]），贝塞尔曲线的公式为：

$$
B(t) = \sum_{i=0}^{n} \binom{n}{i} (1-t)^{n-i} t^i P_i
$$

其中：
- $(\binom{n}{i})$ 是组合数，表示从 $(n)$ 中选 $(i)$ 的组合数。
- $(P_i)$ 是第 $(i)$ 个控制点。
- $(t)$ 是参数，范围从 0 到 1。

### 代码解释

```matlab
function B = bezier_curve(t, control_points)
    n = size(control_points, 1) - 1; % 控制点的数量减一，得到贝塞尔曲线的阶数
    B = zeros(length(t), 2); % 初始化贝塞尔曲线的点
    
    for i = 0:n
        % 计算每个控制点对曲线的贡献
        B = B + nchoosek(n, i) * (1-t).^(n-i)' .* t.^i' * control_points(i+1, :);
    end
end
```

#### 具体步骤和数学公式

1. **函数输入和输出：**
   - 输入参数 `t`：一个包含参数值的向量，用于计算贝塞尔曲线。
   - 输入参数 `control_points`：一个矩阵，每行是一个控制点的 (x, y) 坐标。
   - 输出 `B`：一个矩阵，每行是贝塞尔曲线在对应 `t` 值处的点 (x, y) 坐标。

2. **初始化：**
   ```matlab
   n = size(control_points, 1) - 1; % 控制点的数量减一，得到贝塞尔曲线的阶数
   B = zeros(length(t), 2); % 初始化贝塞尔曲线的点
   ```
   - `n` 是贝塞尔曲线的阶数。
   - `B` 是一个矩阵，用于存储计算出的贝塞尔曲线点，初始值为零。

3. **计算贝塞尔曲线点：**
   ```matlab
   for i = 0:n
       % 计算每个控制点对曲线的贡献
       B = B + nchoosek(n, i) * (1-t).^(n-i)' .* t.^i' * control_points(i+1, :);
   end
   ```
   - `for` 循环遍历所有控制点，每次计算当前控制点对曲线的贡献。
   - `nchoosek(n, i)` 计算组合数 $(\binom{n}{i})$，表示从 $(n)$ 中选 $(i)$ 的组合数。
   - `(1-t).^(n-i)` 计算 $((1-t)^{n-i})$ 的值。
   - `t.^i` 计算 $(t^i)$ 的值。
   - `control_points(i+1, :)` 取出第 $(i+1)$ 个控制点的坐标。
   - `nchoosek(n, i) * (1-t).^(n-i)' .* t.^i' * control_points(i+1, :)` 计算当前控制点对贝塞尔曲线点的贡献，并将其加到 `B` 中。

#### 公式解释

贝塞尔曲线在每个 $(t)$ 值下的点 $(B(t))$ 是所有控制点 $(P_i)$ 贡献的加权和。每个控制点的贡献由以下部分组成：
1. **组合数** $(\binom{n}{i})$：表示当前控制点在贝塞尔曲线中的权重。
2. **基函数** $((1-t)^{n-i} t^i)$：根据 $(t)$ 的值，计算当前控制点对曲线点的影响。
3. **控制点** $(P_i)$：具体的控制点坐标。

贝塞尔曲线的点 $(B(t))$ 是对所有控制点 $(P_i)$ 按照基函数和组合数加权后的求和结果。

通过循环所有控制点，并计算它们对曲线点的贡献，我们可以得到贝塞尔曲线在不同 $(t)$ 值下的点，从而绘制出整个贝塞尔曲线。
