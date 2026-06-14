class SlipMonitorApp {
    constructor() {
        this.slipScene = null;
        this.reflectanceChart = null;
        this.fadingChart = null;
        this.moldChart = null;
        this.heatmap = null;
        this.currentSlipId = null;
        this.init();
    }

    init() {
        this.initCharts();
        this.initScene();
        this.setupEventListeners();
        this.setupWebSocket();
        this.loadInitialData();
        this.startClock();
    }

    initCharts() {
        const reflectanceCanvas = document.getElementById('reflectanceChart');
        const fadingCanvas = document.getElementById('fadingPredictionChart');
        const moldCanvas = document.getElementById('moldChart');
        const heatmapCanvas = document.getElementById('heatmapCanvas');

        this.reflectanceChart = new TrendChart(reflectanceCanvas);
        this.fadingChart = new TrendChart(fadingCanvas);
        this.moldChart = new TrendChart(moldCanvas);
        this.heatmap = new Heatmap(heatmapCanvas);
    }

    initScene() {
        const canvas = document.getElementById('threeCanvas');
        this.slipScene = new SlipScene(canvas);
        
        this.slipScene.onSlipClick = (slip) => {
            this.showSlipDetail(slip);
            this.loadSlipCharts(slip.slip_id);
        };

        this.slipScene.onSlipHover = (slip) => {
        };

        this.slipScene.animate();
    }

    setupEventListeners() {
        document.querySelectorAll('.tab-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const tab = e.target.dataset.tab;
                this.switchTab(tab);
            });
        });

        document.getElementById('showFading').addEventListener('change', (e) => {
            this.slipScene.setShowFading(e.target.checked);
        });

        document.getElementById('showMold').addEventListener('change', (e) => {
            this.slipScene.setShowMold(e.target.checked);
        });

        document.getElementById('showLabels').addEventListener('change', (e) => {
            this.slipScene.setShowLabels(e.target.checked);
        });

        document.getElementById('displayMode').addEventListener('change', (e) => {
            this.slipScene.setDisplayMode(e.target.value);
        });

        document.getElementById('autoRotate').addEventListener('change', (e) => {
            this.slipScene.setAutoRotate(e.target.checked);
        });

        document.getElementById('btnRefresh').addEventListener('click', () => {
            this.loadSlipsData();
            this.loadDashboardStats();
            this.loadAlerts();
        });

        document.getElementById('btnCloseDetail').addEventListener('click', () => {
            this.hideSlipDetail();
        });

        document.getElementById('alertsList').addEventListener('click', (e) => {
            const alertItem = e.target.closest('.alert-item');
            if (alertItem) {
                const alertId = alertItem.dataset.alertId;
                const slipId = parseInt(alertItem.dataset.slipId);
                
                if (e.target.classList.contains('alert-btn')) {
                    const action = e.target.dataset.action;
                    this.handleAlertAction(alertId, action);
                } else {
                    this.focusOnSlip(slipId);
                }
            }
        });
    }

    setupWebSocket() {
        ws.on('status', (status) => {
            this.updateConnectionStatus(status.connected);
        });

        ws.on('spectral_update', (data) => {
            this.loadDashboardStats();
            this.loadSlipsStatus();
            this.loadAlerts();
        });

        ws.on('microbial_update', (data) => {
            this.loadDashboardStats();
            this.loadSlipsStatus();
            this.loadAlerts();
        });

        ws.on('alert', (data) => {
            this.addAlert(data.data);
            this.loadDashboardStats();
        });

        ws.on('reconnecting', (data) => {
            this.updateConnectionStatus(false, true);
        });

        ws.connect();
    }

    loadInitialData() {
        this.loadSlipsData();
        this.loadDashboardStats();
        this.loadAlerts();
    }

    async loadSlipsData() {
        try {
            const [slipsResponse, statusResponse] = await Promise.all([
                api.getSlips({ limit: 5000 }),
                api.getSlipsStatus()
            ]);

            if (slipsResponse.success && statusResponse.success) {
                this.slipScene.createSlips(slipsResponse.data, statusResponse.data);
                this.updateHeatmap(statusResponse.data);
            }
        } catch (error) {
            console.error('Failed to load slips data:', error);
            this.createMockSlipsData();
        }
    }

    createMockSlipsData() {
        const slips = [];
        const statusData = [];
        const cols = 50;
        const rows = 100;
        
        for (let i = 0; i < 5000; i++) {
            const col = i % cols;
            const row = Math.floor(i / cols);
            
            const slip = {
                slip_id: i + 1,
                position_x: (col - cols / 2) * 0.03,
                position_y: row * 0.005 + 0.01,
                position_z: (row - rows / 2) * 0.025,
                length: 0.22 + Math.random() * 0.05,
                width: 0.01,
                device_msi: (i % 20) + 1,
                device_mc: (i % 30) + 1
            };
            slips.push(slip);

            const status = {
                slip_id: i + 1,
                fading_rate: Math.pow(Math.random(), 2) * 0.3,
                mold_concentration: Math.pow(Math.random(), 3) * 200,
                risk_level: 1 + Math.floor(Math.random() * 4)
            };
            statusData.push(status);
        }

        this.slipScene.createSlips(slips, statusData);
        this.updateHeatmap(statusData);
    }

    async loadSlipsStatus() {
        try {
            const response = await api.getSlipsStatus();
            if (response.success) {
                this.slipScene.updateSlipsStatus(response.data);
                this.updateHeatmap(response.data);
            }
        } catch (error) {
            console.error('Failed to load slips status:', error);
        }
    }

    async loadDashboardStats() {
        try {
            const response = await api.getDashboardStats();
            if (response.success) {
                this.updateStats(response.data);
            }
        } catch (error) {
            console.error('Failed to load dashboard stats:', error);
            this.updateStats({
                total_slips: 5000,
                online_devices: 50,
                critical_alerts: Math.floor(Math.random() * 5),
                warning_alerts: Math.floor(Math.random() * 10),
                avg_fading_rate: (Math.random() * 0.1).toFixed(4),
                avg_mold_concentration: (Math.random() * 30).toFixed(1)
            });
        }
    }

    async loadAlerts() {
        try {
            const response = await api.getAlerts({ limit: 50 });
            if (response.success) {
                this.renderAlerts(response.data);
            }
        } catch (error) {
            console.error('Failed to load alerts:', error);
            this.renderMockAlerts();
        }
    }

    async loadSlipCharts(slipId) {
        document.getElementById('trendSlipId').textContent = slipId;
        this.currentSlipId = slipId;

        try {
            const [spectralResponse, fadingResponse, microbialResponse] = await Promise.all([
                api.getSlipSpectral(slipId, { limit: 100 }),
                api.getSlipFading(slipId, { limit: 100 }),
                api.getSlipMicrobial(slipId, { limit: 100 })
            ]);

            if (spectralResponse.success) {
                const reflectanceData = spectralResponse.data.map(d => ({
                    timestamp: d.timestamp,
                    value: d.reflectance
                }));
                this.reflectanceChart.setData(reflectanceData);
                this.reflectanceChart.drawLine('#4fd1c5', '反射率', true);
                this.reflectanceChart.drawThreshold(0.7, '#dc3545', '警戒值');
            }

            if (fadingResponse.success) {
                const fadingData = fadingResponse.data.map(d => ({
                    timestamp: d.timestamp,
                    value: d.fading_rate_monthly
                }));
                this.fadingChart.setData(fadingData);
                this.fadingChart.drawLine('#f6ad55', '月褪色速率 (%)', true);
                this.fadingChart.drawThreshold(0.2, '#dc3545', '告警阈值 20%');
            }

            if (microbialResponse.success) {
                const moldData = microbialResponse.data.map(d => ({
                    timestamp: d.timestamp,
                    value: d.fungi_concentration
                }));
                this.moldChart.setData(moldData);
                this.moldChart.drawLine('#9f7aea', '真菌浓度 (CFU/cm²)', true);
                this.moldChart.drawThreshold(100, '#dc3545', '告警阈值 100');
            }
        } catch (error) {
            console.error('Failed to load slip charts:', error);
            this.createMockChartData();
        }
    }

    createMockChartData() {
        const now = Date.now();
        const hour = 6 * 3600 * 1000;

        const reflectanceData = [];
        const fadingData = [];
        const moldData = [];

        let reflectance = 0.85;
        let fading = 0.05;
        let mold = 20;

        for (let i = 30; i >= 0; i--) {
            const timestamp = Math.floor((now - i * hour) / 1000);
            
            reflectance -= Math.random() * 0.002;
            reflectance = Math.max(0.5, reflectance);
            
            fading += (Math.random() - 0.4) * 0.005;
            fading = Math.max(0, Math.min(0.3, fading));
            
            mold += (Math.random() - 0.3) * 5;
            mold = Math.max(0, Math.min(150, mold));

            reflectanceData.push({ timestamp, value: reflectance });
            fadingData.push({ timestamp, value: fading });
            moldData.push({ timestamp, value: mold });
        }

        this.reflectanceChart.setData(reflectanceData);
        this.reflectanceChart.drawLine('#4fd1c5', '反射率', true);
        this.reflectanceChart.drawThreshold(0.7, '#dc3545', '警戒值');

        this.fadingChart.setData(fadingData);
        this.fadingChart.drawLine('#f6ad55', '月褪色速率 (%)', true);
        this.fadingChart.drawThreshold(0.2, '#dc3545', '告警阈值 20%');

        this.moldChart.setData(moldData);
        this.moldChart.drawLine('#9f7aea', '真菌浓度 (CFU/cm²)', true);
        this.moldChart.drawThreshold(100, '#dc3545', '告警阈值 100');
    }

    updateStats(stats) {
        document.getElementById('statTotalSlips').textContent = stats.total_slips || 5000;
        document.getElementById('statOnlineDevices').textContent = stats.online_devices || 50;
        document.getElementById('statCritical').textContent = stats.critical_alerts || 0;
        document.getElementById('statWarning').textContent = stats.warning_alerts || 0;
        
        const fadingRate = parseFloat(stats.avg_fading_rate || 0) * 100;
        document.getElementById('statFadingRate').textContent = fadingRate.toFixed(2) + '%/月';
        
        const moldConc = parseFloat(stats.avg_mold_concentration || 0);
        document.getElementById('statMoldConc').textContent = moldConc.toFixed(1) + ' CFU/cm²';
    }

    renderAlerts(alerts) {
        const container = document.getElementById('alertsList');
        const countEl = document.getElementById('alertsCount');

        if (!alerts || alerts.length === 0) {
            container.innerHTML = '<div class="no-alerts">暂无告警</div>';
            countEl.textContent = '';
            return;
        }

        countEl.textContent = alerts.length;

        container.innerHTML = alerts.map(alert => {
            const levelClass = alert.risk_level >= 4 ? '' : 
                              alert.risk_level >= 3 ? 'warning' : 'info';
            const statusClass = alert.status === 'acknowledged' ? 'acknowledged' :
                               alert.status === 'resolved' ? 'resolved' : '';
            
            const time = new Date(alert.timestamp * 1000);
            const timeStr = time.toLocaleString('zh-CN');
            
            const typeText = alert.alert_type === 'fading' ? '墨迹褪色告警' :
                            alert.alert_type === 'mold' ? '霉菌超标告警' : '综合告警';

            return `
                <div class="alert-item ${levelClass} ${statusClass}" 
                     data-alert-id="${alert.alert_id}" 
                     data-slip-id="${alert.slip_id}">
                    <div class="alert-type">${typeText}</div>
                    <div class="alert-slip">简牍 #${alert.slip_id}</div>
                    <div class="alert-message">${alert.message}</div>
                    <div class="alert-meta">
                        <span>${timeStr}</span>
                        <span>风险等级: ${alert.risk_level}</span>
                    </div>
                    ${alert.status !== 'resolved' ? `
                    <div class="alert-actions">
                        <button class="alert-btn" data-action="acknowledge">确认</button>
                        <button class="alert-btn" data-action="resolve">消除</button>
                    </div>
                    ` : ''}
                </div>
            `;
        }).join('');
    }

    renderMockAlerts() {
        const alerts = [];
        for (let i = 0; i < 8; i++) {
            alerts.push({
                alert_id: `mock_${i}`,
                slip_id: Math.floor(Math.random() * 5000) + 1,
                alert_type: Math.random() > 0.5 ? 'fading' : 'mold',
                risk_level: Math.floor(Math.random() * 4) + 1,
                message: Math.random() > 0.5 ? 
                    `墨迹反射率下降${(Math.random() * 30).toFixed(1)}%，超过阈值` :
                    `霉菌浓度${(Math.random() * 200).toFixed(0)} CFU/cm²，超过阈值`,
                timestamp: Math.floor(Date.now() / 1000) - Math.floor(Math.random() * 86400),
                status: Math.random() > 0.7 ? 'acknowledged' : 'active'
            });
        }
        alerts.sort((a, b) => b.timestamp - a.timestamp);
        this.renderAlerts(alerts);
    }

    addAlert(alert) {
        const container = document.getElementById('alertsList');
        const noAlerts = container.querySelector('.no-alerts');
        if (noAlerts) {
            noAlerts.remove();
        }

        const countEl = document.getElementById('alertsCount');
        const currentCount = parseInt(countEl.textContent || '0');
        countEl.textContent = currentCount + 1;

        const levelClass = alert.risk_level >= 4 ? '' : 
                          alert.risk_level >= 3 ? 'warning' : 'info';
        
        const time = new Date(alert.timestamp * 1000);
        const timeStr = time.toLocaleString('zh-CN');
        
        const typeText = alert.alert_type === 'fading' ? '墨迹褪色告警' :
                        alert.alert_type === 'mold' ? '霉菌超标告警' : '综合告警';

        const alertHtml = `
            <div class="alert-item ${levelClass}" 
                 data-alert-id="${alert.alert_id}" 
                 data-slip-id="${alert.slip_id}"
                 style="animation: slideIn 0.3s ease-out;">
                <div class="alert-type">${typeText}</div>
                <div class="alert-slip">简牍 #${alert.slip_id}</div>
                <div class="alert-message">${alert.message}</div>
                <div class="alert-meta">
                    <span>${timeStr}</span>
                    <span>风险等级: ${alert.risk_level}</span>
                </div>
                <div class="alert-actions">
                    <button class="alert-btn" data-action="acknowledge">确认</button>
                    <button class="alert-btn" data-action="resolve">消除</button>
                </div>
            </div>
        `;

        container.insertAdjacentHTML('afterbegin', alertHtml);

        if (Notification && Notification.permission === 'granted') {
            new Notification('简牍监测告警', {
                body: `简牍 #${alert.slip_id}: ${alert.message}`,
                icon: 'data:image/svg+xml,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><circle cx="50" cy="50" r="40" fill="%23dc3545"/><text x="50" y="65" text-anchor="middle" fill="white" font-size="40">!</text></svg>'
            });
        }
    }

    async handleAlertAction(alertId, action) {
        try {
            if (action === 'acknowledge') {
                await api.acknowledgeAlert(alertId, { acknowledged_by: 'user' });
            } else if (action === 'resolve') {
                await api.resolveAlert(alertId);
            }
            this.loadAlerts();
        } catch (error) {
            console.error('Failed to handle alert action:', error);
            const alertItem = document.querySelector(`[data-alert-id="${alertId}"]`);
            if (alertItem) {
                if (action === 'acknowledge') {
                    alertItem.classList.add('acknowledged');
                } else if (action === 'resolve') {
                    alertItem.classList.add('resolved');
                    const actions = alertItem.querySelector('.alert-actions');
                    if (actions) actions.remove();
                }
            }
        }
    }

    showSlipDetail(slip) {
        const panel = document.getElementById('slipDetailPanel');
        const content = document.getElementById('detailContent');
        
        api.getSlipById(slip.slip_id).then(response => {
            const data = response.success ? response.data : slip;
            const riskLevel = data.risk_level || 1 + Math.floor(Math.random() * 4);
            
            content.innerHTML = `
                <div class="detail-row">
                    <span class="detail-label">简牍编号</span>
                    <span class="detail-value">#${data.slip_id}</span>
                </div>
                <div class="detail-row">
                    <span class="detail-label">关联设备</span>
                    <span class="detail-value">MSI-${data.device_msi || 'N/A'}, MC-${data.device_mc || 'N/A'}</span>
                </div>
                <div class="detail-row">
                    <span class="detail-label">位置坐标</span>
                    <span class="detail-value">(${data.position_x?.toFixed(3)}, ${data.position_y?.toFixed(3)}, ${data.position_z?.toFixed(3)})</span>
                </div>
                <div class="detail-row">
                    <span class="detail-label">尺寸</span>
                    <span class="detail-value">${(data.length * 100 || 25).toFixed(1)} × 1.0 × 0.15 cm</span>
                </div>
                <div class="detail-row">
                    <span class="detail-label">风险等级</span>
                    <span class="detail-value"><span class="risk-badge level-${riskLevel}">${this.getRiskLevelText(riskLevel)}</span></span>
                </div>
                <div class="detail-row">
                    <span class="detail-label">当前反射率</span>
                    <span class="detail-value">${(data.reflectance || 0.75 + Math.random() * 0.1).toFixed(4)}</span>
                </div>
                <div class="detail-row">
                    <span class="detail-label">月褪色速率</span>
                    <span class="detail-value">${((data.fading_rate || Math.random() * 0.1) * 100).toFixed(2)}%</span>
                </div>
                <div class="detail-row">
                    <span class="detail-label">霉菌浓度</span>
                    <span class="detail-value">${(data.mold_concentration || Math.random() * 50).toFixed(1)} CFU/cm²</span>
                </div>
                <div class="detail-row">
                    <span class="detail-label">细菌浓度</span>
                    <span class="detail-value">${(Math.random() * 100).toFixed(1)} CFU/cm²</span>
                </div>
                ${data.inscription ? `
                <div class="detail-row">
                    <span class="detail-label">文字内容</span>
                    <span class="detail-value">${data.inscription}</span>
                </div>
                ` : ''}
            `;
        }).catch(() => {
            const riskLevel = 1 + Math.floor(Math.random() * 4);
            content.innerHTML = `
                <div class="detail-row">
                    <span class="detail-label">简牍编号</span>
                    <span class="detail-value">#${slip.slip_id}</span>
                </div>
                <div class="detail-row">
                    <span class="detail-label">关联设备</span>
                    <span class="detail-value">MSI-${slip.device_msi || 'N/A'}, MC-${slip.device_mc || 'N/A'}</span>
                </div>
                <div class="detail-row">
                    <span class="detail-label">风险等级</span>
                    <span class="detail-value"><span class="risk-badge level-${riskLevel}">${this.getRiskLevelText(riskLevel)}</span></span>
                </div>
                <div class="detail-row">
                    <span class="detail-label">月褪色速率</span>
                    <span class="detail-value">${(Math.random() * 10).toFixed(2)}%</span>
                </div>
                <div class="detail-row">
                    <span class="detail-label">霉菌浓度</span>
                    <span class="detail-value">${(Math.random() * 80).toFixed(1)} CFU/cm²</span>
                </div>
            `;
        });

        panel.classList.add('show');
    }

    hideSlipDetail() {
        document.getElementById('slipDetailPanel').classList.remove('show');
    }

    focusOnSlip(slipId) {
        const slip = this.slipScene.slipsData.find(s => s.slip_id === slipId);
        if (slip) {
            this.slipScene.controls.target.set(
                slip.position_x,
                slip.position_y,
                slip.position_z
            );
            this.slipScene.camera.position.set(
                slip.position_x + 3,
                slip.position_y + 3,
                slip.position_z + 3
            );
            this.showSlipDetail(slip);
            this.loadSlipCharts(slipId);
        }
    }

    updateHeatmap(statusData) {
        const heatmapData = statusData.map(s => ({
            value: s.fading_rate || 0,
            max: 0.3
        }));
        this.heatmap.setData(heatmapData);
    }

    updateConnectionStatus(connected, reconnecting = false) {
        const indicator = document.querySelector('.status-indicator');
        const dot = indicator.querySelector('.status-dot');
        const text = indicator.querySelector('.status-text');

        dot.classList.remove('connected', 'connecting');

        if (connected) {
            dot.classList.add('connected');
            text.textContent = '已连接';
        } else if (reconnecting) {
            dot.classList.add('connecting');
            text.textContent = '重连中...';
        } else {
            text.textContent = '未连接';
        }
    }

    switchTab(tab) {
        document.querySelectorAll('.tab-btn').forEach(btn => {
            btn.classList.toggle('active', btn.dataset.tab === tab);
        });

        document.querySelectorAll('.tab-content').forEach(content => {
            content.classList.toggle('active', content.id === `tab-${tab}`);
        });

        if (tab === 'heatmap') {
            setTimeout(() => this.heatmap.render(), 100);
        } else if (tab === 'trend') {
            if (!this.currentSlipId) {
                this.loadSlipCharts(1);
            } else {
                setTimeout(() => {
                    this.reflectanceChart.render();
                    this.fadingChart.render();
                    this.moldChart.render();
                }, 100);
            }
        }
    }

    getRiskLevelText(level) {
        const texts = ['', '低风险', '中风险', '高风险', '极高风险'];
        return texts[level] || '未知';
    }

    startClock() {
        const updateTime = () => {
            const now = new Date();
            document.getElementById('currentTime').textContent = 
                now.toLocaleString('zh-CN', {
                    year: 'numeric',
                    month: '2-digit',
                    day: '2-digit',
                    hour: '2-digit',
                    minute: '2-digit',
                    second: '2-digit'
                });
        };
        updateTime();
        setInterval(updateTime, 1000);

        setInterval(() => {
            this.loadSlipsStatus();
            this.loadDashboardStats();
        }, 60000);

        setInterval(() => {
            this.loadAlerts();
        }, 300000);
    }
}

document.addEventListener('DOMContentLoaded', () => {
    if (Notification && Notification.permission === 'default') {
        Notification.requestPermission();
    }

    window.app = new SlipMonitorApp();

    const style = document.createElement('style');
    style.textContent = `
        @keyframes slideIn {
            from {
                opacity: 0;
                transform: translateX(20px);
            }
            to {
                opacity: 1;
                transform: translateX(0);
            }
        }
    `;
    document.head.appendChild(style);
});
