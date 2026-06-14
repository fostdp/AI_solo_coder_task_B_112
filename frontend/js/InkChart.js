class InkChart {
    constructor(options = {}) {
        this.canvas = options.canvas;
        if (!this.canvas) throw new Error('InkChart: canvas is required');
        this.ctx = this.canvas.getContext('2d');
        this.data = options.data || {
            carbon_black: 0.65,
            binder: 0.20,
            moisture: 0.10,
            impurity: 0.05
        };
        this.labels = options.labels || ['炭黑', '胶料', '水分', '杂质'];
        this.colors = options.colors || ['#2c3e50', '#e67e22', '#3498db', '#95a5a6'];
        this.maxValue = options.maxValue || 1.0;
        this.levels = options.levels || 4;
        this.margin = options.margin || 40;
        this.animationProgress = 0;
        this.animationId = null;
    }

    setData(data) {
        this.data = Object.assign({}, this.data, data);
        this.cancelAnimation();
        this.animate();
    }

    animate(duration = 600) {
        const start = performance.now();
        const targetData = Object.assign({}, this.data);
        const step = (now) => {
            const elapsed = now - start;
            const t = Math.min(1, elapsed / duration);
            const ease = t < 0.5 ? 2 * t * t : -1 + (4 - 2 * t) * t;
            this.animationProgress = ease;
            this.render(targetData, ease);
            if (t < 1) {
                this.animationId = requestAnimationFrame(step);
            }
        };
        this.animationId = requestAnimationFrame(step);
    }

    cancelAnimation() {
        if (this.animationId) {
            cancelAnimationFrame(this.animationId);
            this.animationId = null;
        }
    }

    getCenter() {
        return {
            x: this.canvas.width / 2,
            y: this.canvas.height / 2
        };
    }

    getRadius() {
        const minDim = Math.min(this.canvas.width, this.canvas.height);
        return (minDim - 2 * this.margin) / 2;
    }

    render(targetData, progress = 1) {
        const ctx = this.ctx;
        const { x: cx, y: cy } = this.getCenter();
        const R = this.getRadius();
        const n = this.labels.length;

        ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);

        const values = [
            targetData.carbon_black || 0,
            targetData.binder || 0,
            targetData.moisture || 0,
            targetData.impurity || 0
        ];

        for (let level = 1; level <= this.levels; level++) {
            const r = R * (level / this.levels);
            ctx.beginPath();
            for (let i = 0; i <= n; i++) {
                const angle = (Math.PI * 2 * i) / n - Math.PI / 2;
                const px = cx + Math.cos(angle) * r;
                const py = cy + Math.sin(angle) * r;
                if (i === 0) ctx.moveTo(px, py);
                else ctx.lineTo(px, py);
            }
            ctx.closePath();
            ctx.strokeStyle = 'rgba(100, 116, 139, 0.25)';
            ctx.lineWidth = 1;
            ctx.stroke();
        }

        for (let i = 0; i < n; i++) {
            const angle = (Math.PI * 2 * i) / n - Math.PI / 2;
            ctx.beginPath();
            ctx.moveTo(cx, cy);
            ctx.lineTo(cx + Math.cos(angle) * R, cy + Math.sin(angle) * R);
            ctx.strokeStyle = 'rgba(100, 116, 139, 0.3)';
            ctx.lineWidth = 1;
            ctx.stroke();
        }

        ctx.beginPath();
        for (let i = 0; i <= n; i++) {
            const idx = i % n;
            const angle = (Math.PI * 2 * idx) / n - Math.PI / 2;
            const v = Math.min(1, (values[idx] / this.maxValue)) * progress;
            const px = cx + Math.cos(angle) * R * v;
            const py = cy + Math.sin(angle) * R * v;
            if (i === 0) ctx.moveTo(px, py);
            else ctx.lineTo(px, py);
        }
        ctx.closePath();
        ctx.fillStyle = 'rgba(52, 152, 219, 0.3)';
        ctx.fill();
        ctx.strokeStyle = '#3498db';
        ctx.lineWidth = 2;
        ctx.stroke();

        for (let i = 0; i < n; i++) {
            const angle = (Math.PI * 2 * i) / n - Math.PI / 2;
            const v = Math.min(1, (values[i] / this.maxValue)) * progress;
            const px = cx + Math.cos(angle) * R * v;
            const py = cy + Math.sin(angle) * R * v;
            ctx.beginPath();
            ctx.arc(px, py, 4, 0, Math.PI * 2);
            ctx.fillStyle = this.colors[i] || '#3498db';
            ctx.fill();
        }

        for (let i = 0; i < n; i++) {
            const angle = (Math.PI * 2 * i) / n - Math.PI / 2;
            const lx = cx + Math.cos(angle) * (R + 20);
            const ly = cy + Math.sin(angle) * (R + 20);
            const pct = Math.round((values[i] || 0) * 100);
            ctx.fillStyle = '#cbd5e0';
            ctx.font = 'bold 13px Arial';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            ctx.fillText(`${this.labels[i]} ${pct}%`, lx, ly);
        }
    }
}

if (typeof window !== 'undefined') {
    window.InkChart = InkChart;
}
