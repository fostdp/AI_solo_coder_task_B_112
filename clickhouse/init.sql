-- =====================================================
-- 汉代海昏侯墓出土简牍墨迹褪色与微生物防治系统
-- ClickHouse 数据库初始化脚本
-- =====================================================

CREATE DATABASE IF NOT EXISTS haihunhou_slips;

USE haihunhou_slips;

-- =====================================================
-- 1. 简牍基础信息表
-- =====================================================
DROP TABLE IF EXISTS slips;
CREATE TABLE slips (
    slip_id UInt32,
    device_msi UInt16 COMMENT '多光谱成像仪ID (1-20)',
    device_mc UInt16 COMMENT '微生物采样器ID (101-130)',
    position_x Float32 COMMENT '堆叠位置X (0-49)',
    position_y Float32 COMMENT '堆叠位置Y (0-99)',
    position_z Float32 COMMENT '堆叠位置Z（层，0-？）',
    length Float32 DEFAULT 23.0 COMMENT '简牍长度cm',
    width Float32 DEFAULT 1.2 COMMENT '简牍宽度cm',
    inscription String DEFAULT '' COMMENT '释文内容',
    create_time DateTime DEFAULT now()
) ENGINE = MergeTree()
PRIMARY KEY slip_id
ORDER BY slip_id;

-- =====================================================
-- 2. 设备信息表
-- =====================================================
DROP TABLE IF EXISTS devices;
CREATE TABLE devices (
    device_id UInt16,
    device_type Enum('MSI' = 1, 'MC' = 2) COMMENT 'MSI多光谱, MC微生物采样',
    device_name String,
    ip_address String DEFAULT '192.168.1.100',
    port UInt16 DEFAULT 4840,
    status Enum('ONLINE' = 1, 'OFFLINE' = 2, 'ERROR' = 3) DEFAULT 'ONLINE',
    last_heartbeat DateTime DEFAULT now(),
    create_time DateTime DEFAULT now()
) ENGINE = MergeTree()
PRIMARY KEY device_id
ORDER BY device_id;

-- =====================================================
-- 3. 墨迹反射率数据表（时序）
-- =====================================================
DROP TABLE IF EXISTS spectral_data;
CREATE TABLE spectral_data (
    timestamp DateTime,
    device_id UInt16,
    slip_id UInt32,
    wavelength UInt16 COMMENT '波长nm (400-700步长50)',
    reflectance Float32 COMMENT '反射率0-1',
    temperature Float32 COMMENT '环境温度°C',
    humidity Float32 COMMENT '相对湿度%',
    light_intensity Float32 COMMENT '光照强度lux'
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
PRIMARY KEY (slip_id, timestamp)
ORDER BY (slip_id, timestamp, wavelength)
TTL timestamp + INTERVAL 10 YEAR;

-- =====================================================
-- 4. 微生物数据表（时序）
-- =====================================================
DROP TABLE IF EXISTS microbial_data;
CREATE TABLE microbial_data (
    timestamp DateTime,
    device_id UInt16,
    slip_id UInt32,
    fungi_concentration Float32 COMMENT '真菌浓度CFU/cm²',
    bacteria_concentration Float32 COMMENT '细菌浓度CFU/cm²',
    temperature Float32,
    humidity Float32
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
PRIMARY KEY (slip_id, timestamp)
ORDER BY (slip_id, timestamp)
TTL timestamp + INTERVAL 10 YEAR;

-- =====================================================
-- 5. 褪色分析结果表
-- =====================================================
DROP TABLE IF EXISTS fading_analysis;
CREATE TABLE fading_analysis (
    timestamp DateTime,
    slip_id UInt32,
    reflectance_450nm Float32,
    fading_rate_monthly Float32 COMMENT '月褪色速率%',
    predicted_30d Float32,
    predicted_90d Float32,
    predicted_180d Float32,
    risk_level UInt8 COMMENT '1低2中3高4极高'
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
PRIMARY KEY (slip_id, timestamp)
ORDER BY (slip_id, timestamp);

-- =====================================================
-- 6. 霉菌预测结果表
-- =====================================================
DROP TABLE IF EXISTS mold_prediction;
CREATE TABLE mold_prediction (
    timestamp DateTime,
    slip_id UInt32,
    current_concentration Float32,
    predicted_1d Float32,
    predicted_3d Float32,
    predicted_7d Float32,
    risk_level UInt8
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
PRIMARY KEY (slip_id, timestamp)
ORDER BY (slip_id, timestamp);

-- =====================================================
-- 7. 告警表
-- =====================================================
DROP TABLE IF EXISTS alerts;
CREATE TABLE alerts (
    alert_id UUID,
    timestamp DateTime,
    slip_id UInt32,
    alert_type Enum('FADING' = 1, 'MOLD' = 2, 'DEVICE' = 3),
    severity Enum('INFO' = 1, 'WARNING' = 2, 'CRITICAL' = 3),
    message String,
    threshold Float32,
    current_value Float32,
    status Enum('NEW' = 1, 'ACKNOWLEDGED' = 2, 'RESOLVED' = 3, 'CLOSED' = 4) DEFAULT 'NEW',
    acknowledged_by Nullable(String) DEFAULT NULL,
    acknowledged_time Nullable(DateTime) DEFAULT NULL,
    resolved_time Nullable(DateTime) DEFAULT NULL
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
PRIMARY KEY (alert_id, timestamp)
ORDER BY (timestamp, alert_id);

-- =====================================================
-- 8. 环境参数配置表
-- =====================================================
DROP TABLE IF EXISTS system_config;
CREATE TABLE system_config (
    config_key String,
    config_value String,
    description String,
    update_time DateTime DEFAULT now()
) ENGINE = ReplacingMergeTree(update_time)
PRIMARY KEY config_key
ORDER BY config_key;

-- =====================================================
-- 9. 统计视图（物化视图）
-- =====================================================

-- 简牍整体统计
DROP VIEW IF EXISTS slip_stats_mv;
CREATE MATERIALIZED VIEW slip_stats_mv
ENGINE = SummingMergeTree()
PARTITION BY toYYYYMM(timestamp)
PRIMARY KEY (timestamp, risk_level)
ORDER BY (timestamp, risk_level)
AS SELECT
    toStartOfHour(timestamp) as timestamp,
    risk_level,
    count() as count
FROM fading_analysis
GROUP BY timestamp, risk_level;

-- 告警统计
DROP VIEW IF EXISTS alert_stats_mv;
CREATE MATERIALIZED VIEW alert_stats_mv
ENGINE = SummingMergeTree()
PARTITION BY toYYYYMM(timestamp)
PRIMARY KEY (timestamp, alert_type, severity, status)
ORDER BY (timestamp, alert_type, severity, status)
AS SELECT
    toStartOfHour(timestamp) as timestamp,
    alert_type,
    severity,
    status,
    count() as count
FROM alerts
GROUP BY timestamp, alert_type, severity, status;

-- =====================================================
-- 10. 索引优化
-- =====================================================
ALTER TABLE spectral_data ADD INDEX idx_timestamp timestamp TYPE minmax GRANULARITY 1;
ALTER TABLE microbial_data ADD INDEX idx_timestamp timestamp TYPE minmax GRANULARITY 1;
ALTER TABLE alerts ADD INDEX idx_status status TYPE set(10) GRANULARITY 1;

-- =====================================================
-- 11. 简牍缀合匹配结果表
-- =====================================================
DROP TABLE IF EXISTS slip_matches;
CREATE TABLE slip_matches (
    timestamp DateTime DEFAULT now(),
    slip_a UInt32 COMMENT '简牍A ID',
    slip_b UInt32 COMMENT '简牍B ID',
    stroke_similarity Float32 COMMENT '笔锋特征相似度 0-1',
    contour_similarity Float32 COMMENT '边缘轮廓相似度 0-1',
    composite_score Float32 COMMENT '综合匹配度 0-1',
    match_level UInt8 COMMENT '匹配等级:0无,1弱,2中,3强,4极强'
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
PRIMARY KEY (slip_a, slip_b)
ORDER BY (slip_a, slip_b, timestamp)
TTL timestamp + INTERVAL 5 YEAR;

-- =====================================================
-- 12. 墨料成分反演结果表
-- =====================================================
DROP TABLE IF EXISTS ink_composition;
CREATE TABLE ink_composition (
    timestamp DateTime DEFAULT now(),
    slip_id UInt32,
    carbon_black_ratio Float32 COMMENT '炭黑比例 0-1',
    binder_ratio Float32 COMMENT '胶料比例 0-1',
    moisture_ratio Float32 COMMENT '水分比例 0-1',
    impurity_ratio Float32 COMMENT '杂质比例 0-1',
    confidence Float32 COMMENT '预测置信度 0-1',
    ink_type String COMMENT '墨料类型:松烟墨/油烟墨/漆烟墨/混合型墨'
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
PRIMARY KEY (slip_id, timestamp)
ORDER BY (slip_id, timestamp)
TTL timestamp + INTERVAL 10 YEAR;

-- =====================================================
-- 13. 霉菌腐蚀预测结果表
-- =====================================================
DROP TABLE IF EXISTS corrosion_prediction;
CREATE TABLE corrosion_prediction (
    timestamp DateTime DEFAULT now(),
    slip_id UInt32,
    ochratoxin_concentration Float32 COMMENT '赭曲霉毒素A浓度 ppb',
    citrinin_concentration Float32 COMMENT '桔霉素浓度 ppb',
    voc_total Float32 COMMENT '总挥发性有机物 ppm',
    corrosion_factor Float32 COMMENT '腐蚀加剧因子 1-10',
    predicted_damage_rate Float32 COMMENT '预测损伤速率 %/month',
    risk_level UInt8 COMMENT '风险等级:1低,2中,3高,4极高'
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
PRIMARY KEY (slip_id, timestamp)
ORDER BY (slip_id, timestamp)
TTL timestamp + INTERVAL 10 YEAR;

-- =====================================================
-- 14. 微环境优化结果表
-- =====================================================
DROP TABLE IF EXISTS env_optimization;
CREATE TABLE env_optimization (
    timestamp DateTime DEFAULT now(),
    zone_id UInt32 COMMENT '区域ID',
    optimal_temperature Float32 COMMENT '最优温度 °C',
    optimal_humidity Float32 COMMENT '最优湿度 %RH',
    optimal_light_filter Float32 COMMENT '最优紫外过滤率 0-1',
    predicted_lifespan_years Float32 COMMENT '预测保存寿命 年',
    improvement_percent Float32 COMMENT '寿命提升百分比 %',
    current_temperature Float32 COMMENT '当前温度 °C',
    current_humidity Float32 COMMENT '当前湿度 %RH'
) ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
PRIMARY KEY (zone_id, timestamp)
ORDER BY (zone_id, timestamp)
TTL timestamp + INTERVAL 5 YEAR;

-- =====================================================
-- 15. 新表索引优化
-- =====================================================
ALTER TABLE slip_matches ADD INDEX idx_score composite_score TYPE minmax GRANULARITY 1;
ALTER TABLE ink_composition ADD INDEX idx_confidence confidence TYPE minmax GRANULARITY 1;
ALTER TABLE corrosion_prediction ADD INDEX idx_risk risk_level TYPE set(10) GRANULARITY 1;
ALTER TABLE env_optimization ADD INDEX idx_lifespan predicted_lifespan_years TYPE minmax GRANULARITY 1;

SHOW TABLES;
