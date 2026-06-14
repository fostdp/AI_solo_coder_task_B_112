class OptimizePanel {
    constructor(options = {}) {
        this.container = options.container || document.body;
        this.zoneId = options.zoneId || 1;
        this.onOptimize = options.onOptimize || (() => Promise.resolve(null));
        this.isOptimizing = false;
        this.result = null;
        this.currentEnv = options.currentEnv || {
            temperature: 22.0,
            humidity: 55.0,
            light_filter: 0.5
        };
        this.init();
    }

    init() {
        this.el = document.createElement('div');
        this.el.className = 'optimize-panel';

        this.el.innerHTML = `
            <div class="op-header">
                <h4>微环境调控策略优化</h4>
                <button class="op-close" data-role="close">×</button>
            </div>
            <div class="op-body">
                <div class="op-current-env">
                    <h5>当前环境</h5>
                    <div class="op-env-grid">
                        <div class="op-env-item">
                            <span class="op-env-label">温度</span>
                            <span class="op-env-value" data-role="cur-temp">22.0 °C</span>
                        </div>
                        <div class="op-env-item">
                            <span class="op-env-label">湿度</span>
                            <span class="op-env-value" data-role="cur-hum">55.0 %RH</span>
                        </div>
                        <div class="op-env-item">
                            <span class="op-env-label">滤光率</span>
                            <span class="op-env-value" data-role="cur-fil">0.50</span>
                        </div>
                    </div>
                </div>
                <div class="op-controls">
                    <button class="btn btn-optimize" data-role="run">
                        <span class="op-icon">⚙️</span>
                        <span class="op-btn-label">启动贝叶斯优化</span>
                        <span class="op-spinner" style="display:none;">⏳</span>
                    </button>
                    <div class="op-progress" data-role="progress" style="display:none;">
                        <div class="op-progress-bar">
                            <div class="op-progress-fill" data-role="fill"></div>
                        </div>
                        <span class="op-progress-text" data-role="progress-text">优化中 0%</span>
                    </div>
                </div>
                <div class="op-result" data-role="result" style="display:none;">
                    <h5>推荐设定点</h5>
                    <div class="op-result-grid">
                        <div class="op-result-item">
                            <span class="op-result-label">最优温度</span>
                            <span class="op-result-value" data-role="opt-temp">-</span>
                            <span class="op-result-delta" data-role="delta-temp"></span>
                        </div>
                        <div class="op-result-item">
                            <span class="op-result-label">最优湿度</span>
                            <span class="op-result-value" data-role="opt-hum">-</span>
                            <span class="op-result-delta" data-role="delta-hum"></span>
                        </div>
                        <div class="op-result-item">
                            <span class="op-result-label">最优滤光</span>
                            <span class="op-result-value" data-role="opt-fil">-</span>
                            <span class="op-result-delta" data-role="delta-fil"></span>
                        </div>
                        <div class="op-result-item highlight">
                            <span class="op-result-label">寿命改善</span>
                            <span class="op-result-value" data-role="opt-improve">-</span>
                        </div>
                        <div class="op-result-item">
                            <span class="op-result-label">预测寿命</span>
                            <span class="op-result-value" data-role="opt-life">-</span>
                        </div>
                    </div>
                    <div class="op-status" data-role="status"></div>
                    <div class="op-actions">
                        <button class="btn btn-apply" data-role="apply">应用此方案</button>
                        <button class="btn btn-secondary" data-role="rerun">重新优化</button>
                    </div>
                </div>
            </div>
        `;

        this.el.querySelector('[data-role="close"]').addEventListener('click', () => this.hide());
        this.el.querySelector('[data-role="run"]').addEventListener('click', () => this.runOptimization());
        this.el.querySelector('[data-role="rerun"]').addEventListener('click', () => this.runOptimization());
        this.el.querySelector('[data-role="apply"]').addEventListener('click', () => this.applyResult());

        this.updateCurrentEnv();
        this.container.appendChild(this.el);
    }

    updateCurrentEnv(env = this.currentEnv) {
        this.currentEnv = Object.assign({}, this.currentEnv, env);
        this.el.querySelector('[data-role="cur-temp"]').textContent = `${this.currentEnv.temperature.toFixed(1)} °C`;
        this.el.querySelector('[data-role="cur-hum"]').textContent = `${this.currentEnv.humidity.toFixed(1)} %RH`;
        this.el.querySelector('[data-role="cur-fil"]').textContent = this.currentEnv.light_filter.toFixed(2);
    }

    setOptimizing(state) {
        this.isOptimizing = state;
        const btn = this.el.querySelector('[data-role="run"]');
        const spinner = this.el.querySelector('.op-spinner');
        const progress = this.el.querySelector('[data-role="progress"]');
        btn.disabled = state;
        btn.classList.toggle('running', state);
        spinner.style.display = state ? 'inline' : 'none';
        progress.style.display = state ? 'block' : 'none';
        if (state) this.simulateProgress();
    }

    simulateProgress() {
        let pct = 0;
        const fill = this.el.querySelector('[data-role="fill"]');
        const text = this.el.querySelector('[data-role="progress-text"]');
        const tick = () => {
            if (!this.isOptimizing) return;
            pct = Math.min(95, pct + Math.random() * 8);
            fill.style.width = `${pct}%`;
            text.textContent = `优化中 ${Math.round(pct)}%`;
            setTimeout(tick, 250);
        };
        tick();
    }

    async runOptimization() {
        if (this.isOptimizing) return;
        this.setOptimizing(true);
        try {
            this.result = await this.onOptimize(this.zoneId, this.currentEnv);
            if (this.result) this.renderResult();
        } finally {
            this.setOptimizing(false);
            const fill = this.el.querySelector('[data-role="fill"]');
            const text = this.el.querySelector('[data-role="progress-text"]');
            if (fill) fill.style.width = '100%';
            if (text) text.textContent = '优化完成 100%';
        }
    }

    formatDelta(cur, opt, unit = '') {
        const d = opt - cur;
        if (Math.abs(d) < 0.01) return '±0';
        const sign = d > 0 ? '+' : '';
        return `${sign}${d.toFixed(1)}${unit}`;
    }

    renderResult() {
        const r = this.result;
        const resultPanel = this.el.querySelector('[data-role="result"]');
        resultPanel.style.display = 'block';

        this.el.querySelector('[data-role="opt-temp"]').textContent = `${r.optimal_temperature.toFixed(1)} °C`;
        this.el.querySelector('[data-role="opt-hum"]').textContent = `${r.optimal_humidity.toFixed(1)} %RH`;
        this.el.querySelector('[data-role="opt-fil"]').textContent = r.optimal_light_filter.toFixed(2);

        this.el.querySelector('[data-role="delta-temp"]').textContent =
            this.formatDelta(this.currentEnv.temperature, r.optimal_temperature, '°C');
        this.el.querySelector('[data-role="delta-hum"]').textContent =
            this.formatDelta(this.currentEnv.humidity, r.optimal_humidity, '%');
        this.el.querySelector('[data-role="delta-fil"]').textContent =
            this.formatDelta(this.currentEnv.light_filter, r.optimal_light_filter);

        this.el.querySelector('[data-role="opt-improve"]').textContent = `${r.improvement_percent.toFixed(1)}%`;
        this.el.querySelector('[data-role="opt-life"]').textContent = `${r.predicted_lifespan_years.toFixed(0)} 年`;

        const status = this.el.querySelector('[data-role="status"]');
        if (r.improvement_percent > 20) {
            status.innerHTML = '<span class="op-good">✅ 优化结果显著，推荐立即应用</span>';
        } else if (r.improvement_percent > 5) {
            status.innerHTML = '<span class="op-ok">ℹ️ 优化结果有效，可选择性应用</span>';
        } else {
            status.innerHTML = '<span class="op-neutral">⚠️ 当前环境已接近最优</span>';
        }
    }

    applyResult() {
        if (!this.result) return;
        this.updateCurrentEnv({
            temperature: this.result.optimal_temperature,
            humidity: this.result.optimal_humidity,
            light_filter: this.result.optimal_light_filter
        });
        const status = this.el.querySelector('[data-role="status"]');
        if (status) status.innerHTML = '<span class="op-good">✅ 调控方案已下发至设备</span>';
    }

    show() { this.el.style.display = 'block'; }
    hide() { this.el.style.display = 'none'; }
    toggle() { this.el.style.display = this.el.style.display === 'none' ? 'block' : 'none'; }

    destroy() { this.el.remove(); }
}

if (typeof window !== 'undefined') {
    window.OptimizePanel = OptimizePanel;
}
