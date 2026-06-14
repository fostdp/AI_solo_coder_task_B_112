class TrendChart {
    constructor(canvas) {
        this.canvas = canvas;
        this.ctx = canvas.getContext('2d');
        this.data = [];
        this.maxDataPoints = 100;
        this.margin = { top: 20, right: 20, bottom: 30, left: 50 };
    }

    setData(data) {
        this.data = data.slice(-this.maxDataPoints);
        this.render();
    }

    addDataPoint(point) {
        this.data.push(point);
        if (this.data.length > this.maxDataPoints) {
            this.data.shift();
        }
        this.render();
    }

    clear() {
        this.data = [];
        this.render();
    }

    getChartWidth() {
        return this.canvas.width - this.margin.left - this.margin.right;
    }

    getChartHeight() {
        return this.canvas.height - this.margin.top - this.margin.bottom;
    }

    getXScale() {
        return this.data.length > 1 ? this.getChartWidth() / (this.data.length - 1) : 0;
    }

    getYScale() {
        if (this.data.length === 0) return 1;
        const values = this.data.map(d => d.value);
        const max = Math.max(...values, 0.01);
        const min = Math.min(...values, 0);
        const range = max - min || 1;
        return this.getChartHeight() / (range * 1.1);
    }

    getYMin() {
        if (this.data.length === 0) return 0;
        const values = this.data.map(d => d.value);
        const min = Math.min(...values, 0);
        return min;
    }

    drawGrid() {
        const ctx = this.ctx;
        const width = this.getChartWidth();
        const height = this.getChartHeight();
        const left = this.margin.left;
        const top = this.margin.top;

        ctx.strokeStyle = '#2d3748';
        ctx.lineWidth = 1;

        for (let i = 0; i <= 5; i++) {
            const y = top + (height / 5) * i;
            ctx.beginPath();
            ctx.moveTo(left, y);
            ctx.lineTo(left + width, y);
            ctx.stroke();

            const values = this.data.map(d => d.value);
            const max = Math.max(...values, 0.01);
            const min = Math.min(...values, 0);
            const range = max - min || 1;
            const value = max - (range * 1.1) * (i / 5);

            ctx.fillStyle = '#718096';
            ctx.font = '11px Arial';
            ctx.textAlign = 'right';
            ctx.fillText(value.toFixed(2), left - 8, y + 4);
        }

        if (this.data.length > 1) {
            for (let i = 0; i <= 5; i++) {
                const x = left + (width / 5) * i;
                const idx = Math.floor((this.data.length - 1) * (i / 5));
                if (this.data[idx]) {
                    const date = new Date(this.data[idx].timestamp * 1000);
                    ctx.fillStyle = '#718096';
                    ctx.font = '10px Arial';
                    ctx.textAlign = 'center';
                    ctx.fillText(
                        `${date.getMonth() + 1}/${date.getDate()} ${date.getHours()}:00`,
                        x,
                        top + height + 18
                    );
                }
            }
        }
    }

    drawLine(color, label, withFill = false) {
        if (this.data.length < 2) return;

        const ctx = this.ctx;
        const width = this.getChartWidth();
        const height = this.getChartHeight();
        const left = this.margin.left;
        const top = this.margin.top;
        const xScale = this.getXScale();
        const yScale = this.getYScale();
        const yMin = this.getYMin();

        ctx.beginPath();
        ctx.strokeStyle = color;
        ctx.lineWidth = 2;

        for (let i = 0; i < this.data.length; i++) {
            const x = left + i * xScale;
            const y = top + height - (this.data[i].value - yMin) * yScale;

            if (i === 0) {
                ctx.moveTo(x, y);
            } else {
                ctx.lineTo(x, y);
            }
        }
        ctx.stroke();

        if (withFill) {
            ctx.lineTo(left + width, top + height);
            ctx.lineTo(left, top + height);
            ctx.closePath();
            ctx.fillStyle = color + '30';
            ctx.fill();
        }

        if (this.data.length > 0) {
            const lastPoint = this.data[this.data.length - 1];
            const x = left + (this.data.length - 1) * xScale;
            const y = top + height - (lastPoint.value - yMin) * yScale;

            ctx.beginPath();
            ctx.arc(x, y, 4, 0, Math.PI * 2);
            ctx.fillStyle = color;
            ctx.fill();
            ctx.strokeStyle = '#fff';
            ctx.lineWidth = 1;
            ctx.stroke();

            ctx.fillStyle = color;
            ctx.font = 'bold 11px Arial';
            ctx.textAlign = 'left';
            ctx.fillText(lastPoint.value.toFixed(3), x + 8, y - 4);
        }

        if (label) {
            ctx.fillStyle = color;
            ctx.font = '11px Arial';
            ctx.textAlign = 'left';
            ctx.fillText(label, left, top - 5);
        }
    }

    drawThreshold(value, color, label) {
        const ctx = this.ctx;
        const width = this.getChartWidth();
        const height = this.getChartHeight();
        const left = this.margin.left;
        const top = this.margin.top;
        const yScale = this.getYScale();
        const yMin = this.getYMin();

        const y = top + height - (value - yMin) * yScale;

        if (y >= top && y <= top + height) {
            ctx.setLineDash([5, 5]);
            ctx.beginPath();
            ctx.strokeStyle = color;
            ctx.lineWidth = 1;
            ctx.moveTo(left, y);
            ctx.lineTo(left + width, y);
            ctx.stroke();
            ctx.setLineDash([]);

            ctx.fillStyle = color;
            ctx.font = '10px Arial';
            ctx.textAlign = 'right';
            ctx.fillText(label, left + width, y - 4);
        }
    }

    render() {
        this.resizeCanvas();
        this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);

        if (this.data.length > 0) {
            this.drawGrid();
        } else {
            this.ctx.fillStyle = '#718096';
            this.ctx.font = '13px Arial';
            this.ctx.textAlign = 'center';
            this.ctx.fillText(
                '暂无数据',
                this.canvas.width / 2,
                this.canvas.height / 2
            );
        }
    }

    resizeCanvas() {
        const rect = this.canvas.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;
        this.canvas.width = rect.width * dpr;
        this.canvas.height = rect.height * dpr;
        this.ctx.scale(dpr, dpr);
        this.canvas.style.width = rect.width + 'px';
        this.canvas.style.height = rect.height + 'px';
    }
}

class Heatmap {
    constructor(canvas) {
        this.canvas = canvas;
        this.ctx = canvas.getContext('2d');
        this.data = [];
        this.cellSize = 4;
    }

    setData(data) {
        this.data = data;
        this.render();
    }

    render() {
        this.resizeCanvas();
        const ctx = this.ctx;
        const width = this.canvas.width;
        const height = this.canvas.height;

        ctx.clearRect(0, 0, width, height);

        if (this.data.length === 0) {
            ctx.fillStyle = '#718096';
            ctx.font = '13px Arial';
            ctx.textAlign = 'center';
            ctx.fillText('暂无数据', width / 2, height / 2);
            return;
        }

        const cols = Math.ceil(Math.sqrt(this.data.length));
        const rows = Math.ceil(this.data.length / cols);
        const cellW = width / cols;
        const cellH = height / rows;

        this.data.forEach((item, idx) => {
            const col = idx % cols;
            const row = Math.floor(idx / cols);
            const x = col * cellW;
            const y = row * cellH;

            const color = ColorMap.getIntensityGradient(item.value, item.max);
            const hex = ColorMap.colorToHex(color);

            ctx.fillStyle = hex;
            ctx.fillRect(x, y, cellW - 1, cellH - 1);

            if (item.value > item.max * 0.7) {
                ctx.strokeStyle = '#fff';
                ctx.lineWidth = 1;
                ctx.strokeRect(x, y, cellW - 1, cellH - 1);
            }
        });

        this.drawLegend(width, height);
    }

    drawLegend(width, height) {
        const ctx = this.ctx;
        const legendHeight = 15;
        const legendY = height - 25;
        const legendWidth = width - 80;
        const legendX = 40;

        const gradient = ctx.createLinearGradient(legendX, 0, legendX + legendWidth, 0);
        gradient.addColorStop(0, '#000080');
        gradient.addColorStop(0.25, '#0080ff');
        gradient.addColorStop(0.5, '#00ff80');
        gradient.addColorStop(0.75, '#ffff00');
        gradient.addColorStop(1, '#ff0000');

        ctx.fillStyle = gradient;
        ctx.fillRect(legendX, legendY, legendWidth, legendHeight);

        ctx.fillStyle = '#a0aec0';
        ctx.font = '10px Arial';
        ctx.textAlign = 'left';
        ctx.fillText('低', legendX - 25, legendY + 11);
        ctx.textAlign = 'right';
        ctx.fillText('高', legendX + legendWidth + 25, legendY + 11);
    }

    resizeCanvas() {
        const rect = this.canvas.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;
        this.canvas.width = rect.width * dpr;
        this.canvas.height = rect.height * dpr;
        this.ctx.scale(dpr, dpr);
        this.canvas.style.width = rect.width + 'px';
        this.canvas.style.height = rect.height + 'px';
    }
}
