class CorrosionWarning {
    constructor(options = {}) {
        this.container = options.container || document.body;
        this.title = options.title || '霉菌腐蚀因子';
        this.value = options.value || 1.0;
        this.minValue = options.minValue || 1.0;
        this.maxValue = options.maxValue || 10.0;
        this.warnThreshold = options.warnThreshold || 3.0;
        this.critThreshold = options.critThreshold || 6.0;
        this.lastUpdated = options.lastUpdated || null;
        this.history = options.history || [];
        this.init();
    }

    init() {
        this.el = document.createElement('div');
        this.el.className = 'corrosion-warning-panel';

        this.el.innerHTML = `
            <div class="cw-header">
                <h4 class="cw-title">${this.title}</h4>
                <span class="cw-badge" data-role="badge">正常</span>
            </div>
            <div class="cw-gauge-container">
                <canvas class="cw-gauge-canvas" width="240" height="140"></canvas>
                <div class="cw-gauge-value" data-role="value">1.00</div>
            </div>
            <div class="cw-meta">
                <div class="cw-meta-item">
                    <span class="cw-meta-label">风险等级</span>
                    <span class="cw-meta-value" data-role="level">低</span>
                </div>
                <div class="cw-meta-item">
                    <span class="cw-meta-label">最后更新</span>
                    <span class="cw-meta-value" data-role="time">-</span>
                </div>
            </div>
            <div class="cw-scale">
                <span>1.0</span>
                <span class="mid">3.0 (警告)</span>
                <span class="mid">6.0 (严重)</span>
                <span>10.0</span>
            </div>
        `;

        this.canvas = this.el.querySelector('.cw-gauge-canvas');
        this.ctx = this.canvas.getContext('2d');
        this.valueEl = this.el.querySelector('[data-role="value"]');
        this.badgeEl = this.el.querySelector('[data-role="badge"]');
        this.levelEl = this.el.querySelector('[data-role="level"]');
        this.timeEl = this.el.querySelector('[data-role="time"]');

        this.container.appendChild(this.el);
        this.setValue(this.value, this.lastUpdated);
    }

    getRiskLevel(v) {
        if (v < this.warnThreshold) return { level: 'low', label: '低', color: '#28a745', badge: '正常' };
        if (v < this.critThreshold) return { level: 'warn', label: '中', color: '#f39c12', badge: '警告' };
        return { level: 'crit', label: '高', color: '#dc3545', badge: '严重' };
    }

    setValue(value, timestamp = null) {
        this.value = Math.max(this.minValue, Math.min(this.maxValue, value));
        if (timestamp) {
            this.lastUpdated = timestamp;
            const d = new Date(timestamp * 1000);
            this.timeEl.textContent = `${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}`;
        }

        this.valueEl.textContent = this.value.toFixed(2);

        const risk = this.getRiskLevel(this.value);
        this.badgeEl.textContent = risk.badge;
        this.badgeEl.className = `cw-badge cw-${risk.level}`;
        this.levelEl.textContent = risk.label;
        this.levelEl.style.color = risk.color;

        this.history.push({ t: Date.now(), v: this.value });
        if (this.history.length > 50) this.history.shift();

        this.renderGauge();
    }

    renderGauge() {
        const ctx = this.ctx;
        const w = this.canvas.width;
        const h = this.canvas.height;
        ctx.clearRect(0, 0, w, h);

        const cx = w / 2;
        const cy = h * 0.85;
        const r = Math.min(w, h * 1.8) * 0.42;

        const drawArc = (startAngle, endAngle, color, width) => {
            ctx.beginPath();
            ctx.arc(cx, cy, r, startAngle, endAngle);
            ctx.strokeStyle = color;
            ctx.lineWidth = width;
            ctx.lineCap = 'round';
            ctx.stroke();
        };

        const bgStart = Math.PI;
        const bgEnd = Math.PI * 2;
        drawArc(bgStart, bgEnd, '#e9ecef', 18);

        const warnStartAng = bgStart + ((this.warnThreshold - this.minValue) / (this.maxValue - this.minValue)) * Math.PI;
        const critStartAng = bgStart + ((this.critThreshold - this.minValue) / (this.maxValue - this.minValue)) * Math.PI;
        drawArc(bgStart, warnStartAng, '#28a745', 14);
        drawArc(warnStartAng, critStartAng, '#f39c12', 14);
        drawArc(critStartAng, bgEnd, '#dc3545', 14);

        const ratio = (this.value - this.minValue) / (this.maxValue - this.minValue);
        const needleAngle = bgStart + ratio * Math.PI;
        drawArc(bgStart, needleAngle, this.getRiskLevel(this.value).color, 10);

        ctx.save();
        ctx.translate(cx, cy);
        ctx.rotate(needleAngle);
        ctx.beginPath();
        ctx.moveTo(0, -4);
        ctx.lineTo(r * 0.95, 0);
        ctx.lineTo(0, 4);
        ctx.closePath();
        ctx.fillStyle = '#1a202c';
        ctx.fill();
        ctx.restore();

        ctx.beginPath();
        ctx.arc(cx, cy, 8, 0, Math.PI * 2);
        ctx.fillStyle = '#1a202c';
        ctx.fill();
    }

    destroy() {
        this.el.remove();
    }
}

if (typeof window !== 'undefined') {
    window.CorrosionWarning = CorrosionWarning;
}
