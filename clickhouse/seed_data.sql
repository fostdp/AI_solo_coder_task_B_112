-- =====================================================
-- 种子数据脚本
-- 用于初始化简牍信息、设备信息和历史监测数据
-- =====================================================

USE haihunhou_slips;

-- =====================================================
-- 1. 插入设备信息（20台多光谱成像仪 + 30台微生物采样器）
-- =====================================================
TRUNCATE TABLE devices;

INSERT INTO devices (device_id, device_type, device_name, ip_address, port)
SELECT
    number + 1 as device_id,
    'MSI' as device_type,
    '多光谱成像仪-' || toString(number + 1) as device_name,
    '192.168.1.' || toString(10 + number) as ip_address,
    4840 as port
FROM numbers(20);

INSERT INTO devices (device_id, device_type, device_name, ip_address, port)
SELECT
    101 + number as device_id,
    'MC' as device_type,
    '微生物采样器-' || toString(number + 1) as device_name,
    '192.168.1.' || toString(50 + number) as ip_address,
    4840 as port
FROM numbers(30);

-- =====================================================
-- 2. 插入简牍信息（5000枚）
-- 布局：50列 × 100行 = 5000枚
-- 每250枚简牍共享一台多光谱成像仪
-- 每167枚简牍共享一台微生物采样器
-- =====================================================
TRUNCATE TABLE slips;

INSERT INTO slips (slip_id, device_msi, device_mc, position_x, position_y, position_z, length, width, inscription)
SELECT
    slip_id,
    toUInt16(intDiv(slip_id - 1, 250) + 1) as device_msi,
    toUInt16(101 + intDiv(slip_id - 1, 167)) as device_mc,
    toFloat32(intDiv(slip_id - 1, 100) % 50) as position_x,
    toFloat32((slip_id - 1) % 100) as position_y,
    toFloat32(intDiv(slip_id - 1, 5000)) as position_z,
    22.5 + rand() % 15 / 10.0 as length,
    1.0 + rand() % 5 / 10.0 as width,
    '' as inscription
FROM (
    SELECT number + 1 as slip_id
    FROM numbers(5000)
);

-- =====================================================
-- 3. 插入系统配置参数
-- =====================================================
TRUNCATE TABLE system_config;

INSERT INTO system_config (config_key, config_value, description) VALUES
('alert.fading.threshold', '20.0', '墨迹褪色月下降阈值%'),
('alert.mold.threshold', '100.0', '霉菌浓度阈值CFU/cm²'),
('notification.dingtalk.webhook', 'https://oapi.dingtalk.com/robot/send?access_token=YOUR_TOKEN', '钉钉机器人Webhook'),
('notification.dingtalk.secret', 'YOUR_SECRET', '钉钉机器人签名密钥'),
('notification.smtp.host', 'smtp.example.com', 'SMTP服务器地址'),
('notification.smtp.port', '465', 'SMTP服务器端口'),
('notification.smtp.user', 'noreply@example.com', 'SMTP用户名'),
('notification.smtp.password', 'YOUR_PASSWORD', 'SMTP密码'),
('notification.smtp.from', 'noreply@example.com', '发件人地址'),
('notification.smtp.to', 'admin@example.com,conservator@example.com', '收件人地址，多个用逗号分隔'),
('model.fading.A1', '1.2e-8', '光氧化指前因子'),
('model.fading.Ea1', '45.0', '光氧化活化能kJ/mol'),
('model.fading.alpha', '0.8', '波长指数'),
('model.fading.A2', '8.5e-7', '水解指前因子'),
('model.fading.Ea2', '55.0', '水解活化能kJ/mol'),
('model.fading.beta', '1.5', '湿度指数'),
('model.mold.beta0', '-2.5', '响应面常数项'),
('model.mold.beta1', '0.12', '温度一次项系数'),
('model.mold.beta2', '0.08', '湿度一次项系数'),
('model.mold.beta3', '-0.0015', '温度二次项系数'),
('model.mold.beta4', '-0.0008', '湿度二次项系数'),
('model.mold.beta5', '0.0005', '交互相系数'),
('opcua.poll_interval', '21600', 'OPC UA数据采集间隔秒(6小时)'),
('opcua.timeout', '10000', 'OPC UA连接超时毫秒');

-- =====================================================
-- 4. 生成历史光谱数据（过去30天，每6小时一次，共120个时间点）
-- 简化：为每枚简牍生成7个波长的反射率数据
-- =====================================================

-- 插入过去30天的光谱数据（抽样10%的简牍以控制数据量，完整数据由模拟器生成）
INSERT INTO spectral_data (timestamp, device_id, slip_id, wavelength, reflectance, temperature, humidity, light_intensity)
SELECT
    toDateTime('2026-05-15 00:00:00') + interval 6 * number hour as timestamp,
    toUInt16(intDiv(s.slip_id - 1, 250) + 1) as device_id,
    s.slip_id,
    toUInt16(wavelength) as wavelength,
    -- 反射率随时间缓慢下降，叠加随机噪声
    0.75 - 0.001 * number + (rand() % 100) / 1000.0 as reflectance,
    18.0 + (rand() % 80) / 10.0 as temperature,
    55.0 + (rand() % 150) / 10.0 as humidity,
    45.0 + (rand() % 200) / 10.0 as light_intensity
FROM slips s
CROSS JOIN (
    SELECT number FROM numbers(120)  -- 30天 × 4次/天
) t
CROSS JOIN (
    SELECT arrayJoin([400, 450, 500, 550, 600, 650, 700]) as wavelength
) w
WHERE s.slip_id % 10 = 0;  -- 抽样10%

-- =====================================================
-- 5. 生成历史微生物数据
-- =====================================================
INSERT INTO microbial_data (timestamp, device_id, slip_id, fungi_concentration, bacteria_concentration, temperature, humidity)
SELECT
    toDateTime('2026-05-15 00:00:00') + interval 6 * number hour as timestamp,
    toUInt16(101 + intDiv(s.slip_id - 1, 167)) as device_id,
    s.slip_id,
    -- 霉菌浓度基于温湿度响应面模型
    exp(-2.5 + 0.12 * (18 + (rand() % 80) / 10.0) + 0.08 * (55 + (rand() % 150) / 10.0)
        - 0.0015 * pow(18 + (rand() % 80) / 10.0, 2)
        - 0.0008 * pow(55 + (rand() % 150) / 10.0, 2)
        + 0.0005 * (18 + (rand() % 80) / 10.0) * (55 + (rand() % 150) / 10.0))
        + (rand() % 50) / 10.0 as fungi_concentration,
    15.0 + (rand() % 100) / 10.0 as bacteria_concentration,
    18.0 + (rand() % 80) / 10.0 as temperature,
    55.0 + (rand() % 150) / 10.0 as humidity
FROM slips s
CROSS JOIN (
    SELECT number FROM numbers(120)
) t
WHERE s.slip_id % 10 = 0;

-- =====================================================
-- 6. 生成褪色分析结果（部分简牍）
-- =====================================================
INSERT INTO fading_analysis (timestamp, slip_id, reflectance_450nm, fading_rate_monthly, predicted_30d, predicted_90d, predicted_180d, risk_level)
SELECT
    max(timestamp) as timestamp,
    slip_id,
    avgIf(reflectance, wavelength = 450) as reflectance_450nm,
    -- 计算月褪色速率
    (maxIf(reflectance, timestamp = min_ts) - minIf(reflectance, timestamp = max_ts)) / 30.0 * 100 as fading_rate_monthly,
    avgIf(reflectance, wavelength = 450) * 0.95 as predicted_30d,
    avgIf(reflectance, wavelength = 450) * 0.88 as predicted_90d,
    avgIf(reflectance, wavelength = 450) * 0.78 as predicted_180d,
    -- 风险等级评估
    CASE
        WHEN (maxIf(reflectance, timestamp = min_ts) - minIf(reflectance, timestamp = max_ts)) / 30.0 * 100 > 20 THEN 4
        WHEN (maxIf(reflectance, timestamp = min_ts) - minIf(reflectance, timestamp = max_ts)) / 30.0 * 100 > 10 THEN 3
        WHEN (maxIf(reflectance, timestamp = min_ts) - minIf(reflectance, timestamp = max_ts)) / 30.0 * 100 > 5 THEN 2
        ELSE 1
    END as risk_level
FROM spectral_data
CROSS JOIN (
    SELECT min(timestamp) as min_ts, max(timestamp) as max_ts FROM spectral_data
) ts
GROUP BY slip_id;

-- =====================================================
-- 7. 生成霉菌预测结果（部分简牍）
-- =====================================================
INSERT INTO mold_prediction (timestamp, slip_id, current_concentration, predicted_1d, predicted_3d, predicted_7d, risk_level)
SELECT
    max(timestamp) as timestamp,
    slip_id,
    max(fungi_concentration) as current_concentration,
    max(fungi_concentration) * 1.1 as predicted_1d,
    max(fungi_concentration) * 1.3 as predicted_3d,
    max(fungi_concentration) * 1.6 as predicted_7d,
    CASE
        WHEN max(fungi_concentration) > 100 THEN 4
        WHEN max(fungi_concentration) > 70 THEN 3
        WHEN max(fungi_concentration) > 40 THEN 2
        ELSE 1
    END as risk_level
FROM microbial_data
GROUP BY slip_id;

-- =====================================================
-- 8. 生成示例告警数据
-- =====================================================
INSERT INTO alerts (alert_id, timestamp, slip_id, alert_type, severity, message, threshold, current_value, status)
SELECT
    generateUUIDv4() as alert_id,
    now() as timestamp,
    slip_id,
    'FADING' as alert_type,
    'CRITICAL' as severity,
    '简牍#' || toString(slip_id) || ' 墨迹褪色速率超过阈值' as message,
    20.0 as threshold,
    fading_rate_monthly as current_value,
    'NEW' as status
FROM fading_analysis
WHERE fading_rate_monthly > 20.0
LIMIT 5;

INSERT INTO alerts (alert_id, timestamp, slip_id, alert_type, severity, message, threshold, current_value, status)
SELECT
    generateUUIDv4() as alert_id,
    now() as timestamp,
    slip_id,
    'MOLD' as alert_type,
    'WARNING' as severity,
    '简牍#' || toString(slip_id) || ' 霉菌浓度超过安全阈值' as message,
    100.0 as threshold,
    current_concentration as current_value,
    'NEW' as status
FROM mold_prediction
WHERE current_concentration > 100.0
LIMIT 3;

-- =====================================================
-- 数据统计
-- =====================================================
SELECT '简牍总数: ', count() FROM slips;
SELECT '设备总数: ', count() FROM devices;
SELECT '光谱数据量: ', count() FROM spectral_data;
SELECT '微生物数据量: ', count() FROM microbial_data;
SELECT '褪色分析结果: ', count() FROM fading_analysis;
SELECT '霉菌预测结果: ', count() FROM mold_prediction;
SELECT '告警数量: ', count() FROM alerts;
