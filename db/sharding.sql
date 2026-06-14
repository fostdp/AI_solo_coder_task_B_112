-- ============================================================
-- 海昏侯简牍监测系统 - ClickHouse 分片键优化脚本
-- 修复：数据倾斜问题 - 改用随机分片键
-- ============================================================

USE haihunhou;

-- ============================================================
-- 1. 创建集群定义（如使用集群模式）
-- ============================================================
-- 注意：根据实际ClickHouse集群配置修改
-- CREATE CLUSTER IF NOT EXISTS haihunhou_cluster
--     SETTINGS replication_factor = 2;

-- ============================================================
-- 2. 修改 spectral_data 表 - 使用 rand() 作为分片键
--    原分片键：toYYYYMM(timestamp) - 导致按月集中
--    新分片键：rand() - 均匀分布到各节点
-- ============================================================

-- 如果已存在旧表，需要迁移数据
-- 先创建新表（带随机分片键）
CREATE TABLE IF NOT EXISTS spectral_data_v2
(
    timestamp DateTime CODEC(DoubleDelta, LZ4),
    device_id UInt16,
    slip_id UInt32,
    wavelength UInt16,
    reflectance Float32 CODEC(Gorilla, LZ4),
    temperature Float32 CODEC(Gorilla, LZ4),
    humidity Float32 CODEC(Gorilla, LZ4),
    light_intensity Float32 CODEC(Gorilla, LZ4),
    shard_key UInt64 DEFAULT rand()
)
ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (shard_key, slip_id, timestamp)
PRIMARY KEY (shard_key, slip_id, timestamp)
SAMPLE BY slip_id
TTL timestamp + INTERVAL 10 YEAR
SETTINGS
    index_granularity = 8192,
    min_bytes_for_wide_part = '10M',
    max_parts_in_total = 100000;

-- ============================================================
-- 3. 修改 microbial_data 表 - 使用 rand() 作为分片键
-- ============================================================
CREATE TABLE IF NOT EXISTS microbial_data_v2
(
    timestamp DateTime CODEC(DoubleDelta, LZ4),
    device_id UInt16,
    slip_id UInt32,
    fungi_concentration Float32 CODEC(Gorilla, LZ4),
    bacteria_concentration Float32 CODEC(Gorilla, LZ4),
    temperature Float32 CODEC(Gorilla, LZ4),
    humidity Float32 CODEC(Gorilla, LZ4),
    shard_key UInt64 DEFAULT rand()
)
ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (shard_key, slip_id, timestamp)
PRIMARY KEY (shard_key, slip_id, timestamp)
SAMPLE BY slip_id
TTL timestamp + INTERVAL 10 YEAR
SETTINGS
    index_granularity = 8192,
    min_bytes_for_wide_part = '10M',
    max_parts_in_total = 100000;

-- ============================================================
-- 4. 修改 fading_analysis 表
-- ============================================================
CREATE TABLE IF NOT EXISTS fading_analysis_v2
(
    timestamp DateTime,
    slip_id UInt32,
    reflectance_450nm Float32,
    fading_rate_monthly Float32,
    predicted_30d Float32,
    predicted_90d Float32,
    predicted_180d Float32,
    risk_level UInt8,
    material String DEFAULT 'UNKNOWN',
    shard_key UInt64 DEFAULT rand()
)
ENGINE = ReplacingMergeTree(timestamp)
PARTITION BY toYYYYMM(timestamp)
ORDER BY (shard_key, slip_id, timestamp)
PRIMARY KEY (shard_key, slip_id)
SETTINGS
    index_granularity = 8192;

-- ============================================================
-- 5. 修改 mold_prediction 表
-- ============================================================
CREATE TABLE IF NOT EXISTS mold_prediction_v2
(
    timestamp DateTime,
    slip_id UInt32,
    current_concentration Float32,
    growth_rate Float32,
    predicted_7d Float32,
    predicted_30d Float32,
    predicted_90d Float32,
    risk_level UInt8,
    material String DEFAULT 'UNKNOWN',
    shard_key UInt64 DEFAULT rand()
)
ENGINE = ReplacingMergeTree(timestamp)
PARTITION BY toYYYYMM(timestamp)
ORDER BY (shard_key, slip_id, timestamp)
PRIMARY KEY (shard_key, slip_id)
SETTINGS
    index_granularity = 8192;

-- ============================================================
-- 6. 修改 alerts 表
-- ============================================================
CREATE TABLE IF NOT EXISTS alerts_v2
(
    alert_id String,
    timestamp DateTime,
    slip_id UInt32,
    alert_type String,
    risk_level UInt8,
    message String,
    status String DEFAULT 'active',
    acknowledged_by String DEFAULT '',
    acknowledged_at DateTime DEFAULT toDateTime(0),
    resolved_at DateTime DEFAULT toDateTime(0),
    shard_key UInt64 DEFAULT rand()
)
ENGINE = ReplacingMergeTree(timestamp)
PARTITION BY toYYYYMM(timestamp)
ORDER BY (shard_key, timestamp, alert_id)
PRIMARY KEY (shard_key, alert_id)
SETTINGS
    index_granularity = 8192;

-- ============================================================
-- 7. 数据迁移（从旧表迁移到新表）
-- ============================================================

-- 迁移光谱数据
ALTER TABLE spectral_data_v2 ADD COLUMN IF NOT EXISTS shard_key UInt64 DEFAULT rand();
INSERT INTO spectral_data_v2
    (timestamp, device_id, slip_id, wavelength, reflectance,
     temperature, humidity, light_intensity, shard_key)
SELECT
    timestamp, device_id, slip_id, wavelength, reflectance,
    temperature, humidity, light_intensity,
    rand() AS shard_key
FROM spectral_data;

-- 迁移微生物数据
ALTER TABLE microbial_data_v2 ADD COLUMN IF NOT EXISTS shard_key UInt64 DEFAULT rand();
INSERT INTO microbial_data_v2
    (timestamp, device_id, slip_id, fungi_concentration, bacteria_concentration,
     temperature, humidity, shard_key)
SELECT
    timestamp, device_id, slip_id, fungi_concentration, bacteria_concentration,
    temperature, humidity,
    rand() AS shard_key
FROM microbial_data;

-- ============================================================
-- 8. 原子表切换
-- ============================================================
-- 注意：以下操作需要谨慎，建议在维护窗口执行
-- RENAME TABLE
--     spectral_data TO spectral_data_legacy,
--     spectral_data_v2 TO spectral_data,
--     microbial_data TO microbial_data_legacy,
--     microbial_data_v2 TO microbial_data,
--     fading_analysis TO fading_analysis_legacy,
--     fading_analysis_v2 TO fading_analysis,
--     mold_prediction TO mold_prediction_legacy,
--     mold_prediction_v2 TO mold_prediction,
--     alerts TO alerts_legacy,
--     alerts_v2 TO alerts;

-- ============================================================
-- 9. 删除旧表（确认无问题后执行）
-- ============================================================
-- DROP TABLE IF EXISTS spectral_data_legacy;
-- DROP TABLE IF EXISTS microbial_data_legacy;
-- DROP TABLE IF EXISTS fading_analysis_legacy;
-- DROP TABLE IF EXISTS mold_prediction_legacy;
-- DROP TABLE IF EXISTS alerts_legacy;

-- ============================================================
-- 10. 重建物化视图
-- ============================================================

DROP VIEW IF EXISTS mv_slip_fading_summary;

CREATE MATERIALIZED VIEW mv_slip_fading_summary
ENGINE = SummingMergeTree()
PARTITION BY toYYYYMM(date)
ORDER BY (date, slip_id, risk_level)
AS
SELECT
    toDate(timestamp) AS date,
    slip_id,
    risk_level,
    count() AS record_count,
    avg(reflectance_450nm) AS avg_reflectance,
    avg(fading_rate_monthly) AS avg_fading_rate
FROM fading_analysis_v2
GROUP BY date, slip_id, risk_level;

DROP VIEW IF EXISTS mv_slip_mold_summary;

CREATE MATERIALIZED VIEW mv_slip_mold_summary
ENGINE = SummingMergeTree()
PARTITION BY toYYYYMM(date)
ORDER BY (date, slip_id, risk_level)
AS
SELECT
    toDate(timestamp) AS date,
    slip_id,
    risk_level,
    count() AS record_count,
    avg(current_concentration) AS avg_concentration,
    avg(growth_rate) AS avg_growth_rate
FROM mold_prediction_v2
GROUP BY date, slip_id, risk_level;

-- ============================================================
-- 11. 分布式DDL（集群模式）
-- ============================================================
-- 如使用集群，将上述CREATE TABLE语句改为：
-- CREATE TABLE IF NOT EXISTS spectral_data_v2 ON CLUSTER haihunhou_cluster ...
-- ENGINE = Distributed(haihunhou_cluster, haihunhou, spectral_data_v2, shard_key);

-- ============================================================
-- 12. 分片键选择验证查询
-- ============================================================
-- 验证分片是否均匀：
-- SELECT
--     shard_key % 5 AS shard_id,
--     count() AS rows,
--     round(rows / total() * 100, 2) AS percent
-- FROM spectral_data_v2
-- GROUP BY shard_id
-- ORDER BY shard_id;

-- 验证查询性能：
-- SELECT count(), min(timestamp), max(timestamp)
-- FROM spectral_data_v2
-- PREWHERE slip_id = 1234
--   AND timestamp >= now() - INTERVAL 7 DAY;

-- ============================================================
-- 优化说明：
-- ============================================================
-- 原问题：
--   按月分区 + 按 (slip_id, timestamp) 排序 -> 同一slip数据集中在一个分片
--   热点slip（如高风险）导致单节点压力过大
--
-- 优化方案：
--   新增 shard_key UInt64 DEFAULT rand() 列
--   ORDER BY (shard_key, slip_id, timestamp)
--   PRIMARY KEY (shard_key, slip_id)
--
-- 效果：
--   1. 数据随机分布到各分片节点，消除热点
--   2. 同一slip的数据仍然按时间有序（在分片内）
--   3. SAMPLE BY slip_id 保留采样查询能力
--   4. 保留原有的按月分区和TTL策略
--
-- 权衡：
--   - 按slip_id点查需要扫描多个分片 -> 使用二级索引补偿
--   - 增加了一列8字节 -> 存储增加约3%（可接受）
