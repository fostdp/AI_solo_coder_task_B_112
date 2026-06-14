const ColorMap = {
    riskColors: {
        1: { r: 0.157, g: 0.655, b: 0.271 },
        2: { r: 1.0, g: 0.757, b: 0.027 },
        3: { r: 0.992, g: 0.494, b: 0.078 },
        4: { r: 0.863, g: 0.208, b: 0.271 }
    },

    getRiskColor(level) {
        const clampedLevel = Math.max(1, Math.min(4, level));
        return this.riskColors[clampedLevel];
    },

    getRiskColorHex(level) {
        const colors = ['#28a745', '#ffc107', '#fd7e14', '#dc3545'];
        const clampedLevel = Math.max(1, Math.min(4, level));
        return colors[clampedLevel - 1];
    },

    interpolateColor(color1, color2, t) {
        return {
            r: color1.r + (color2.r - color1.r) * t,
            g: color1.g + (color2.g - color1.g) * t,
            b: color1.b + (color2.b - color1.b) * t
        };
    },

    getFadingColor(fadingRate) {
        if (fadingRate < 0.05) {
            return { r: 0.157, g: 0.655, b: 0.271 };
        } else if (fadingRate < 0.1) {
            const t = (fadingRate - 0.05) / 0.05;
            return this.interpolateColor(
                { r: 0.157, g: 0.655, b: 0.271 },
                { r: 1.0, g: 0.757, b: 0.027 },
                t
            );
        } else if (fadingRate < 0.2) {
            const t = (fadingRate - 0.1) / 0.1;
            return this.interpolateColor(
                { r: 1.0, g: 0.757, b: 0.027 },
                { r: 0.992, g: 0.494, b: 0.078 },
                t
            );
        } else {
            const t = Math.min(1, (fadingRate - 0.2) / 0.2);
            return this.interpolateColor(
                { r: 0.992, g: 0.494, b: 0.078 },
                { r: 0.863, g: 0.208, b: 0.271 },
                t
            );
        }
    },

    getMoldColor(concentration) {
        if (concentration < 10) {
            return { r: 0.157, g: 0.655, b: 0.271 };
        } else if (concentration < 50) {
            const t = (concentration - 10) / 40;
            return this.interpolateColor(
                { r: 0.157, g: 0.655, b: 0.271 },
                { r: 0.553, g: 0.267, b: 0.678 },
                t
            );
        } else if (concentration < 100) {
            const t = (concentration - 50) / 50;
            return this.interpolateColor(
                { r: 0.553, g: 0.267, b: 0.678 },
                { r: 0.839, g: 0.188, b: 0.631 },
                t
            );
        } else {
            const t = Math.min(1, (concentration - 100) / 100);
            return this.interpolateColor(
                { r: 0.839, g: 0.188, b: 0.631 },
                { r: 0.573, g: 0.0, b: 0.349 },
                t
            );
        }
    },

    getCompositeColor(fadingRate, moldConcentration) {
        const fadingColor = this.getFadingColor(fadingRate);
        const moldColor = this.getMoldColor(moldConcentration);
        const fadingWeight = Math.min(1, fadingRate / 0.2);
        const moldWeight = Math.min(1, moldConcentration / 100);
        const totalWeight = fadingWeight + moldWeight;

        if (totalWeight === 0) {
            return { r: 0.157, g: 0.655, b: 0.271 };
        }

        return {
            r: (fadingColor.r * fadingWeight + moldColor.r * moldWeight) / totalWeight,
            g: (fadingColor.g * fadingWeight + moldColor.g * moldWeight) / totalWeight,
            b: (fadingColor.b * fadingWeight + moldColor.b * moldWeight) / totalWeight
        };
    },

    colorToHex(color) {
        const r = Math.round(color.r * 255);
        const g = Math.round(color.g * 255);
        const b = Math.round(color.b * 255);
        return `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}`;
    },

    hexToRgb(hex) {
        const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
        return result ? {
            r: parseInt(result[1], 16) / 255,
            g: parseInt(result[2], 16) / 255,
            b: parseInt(result[3], 16) / 255
        } : { r: 0, g: 0, b: 0 };
    },

    getMoldParticleColor(concentration) {
        const baseColors = [
            { r: 0.553, g: 0.267, b: 0.678 },
            { r: 0.839, g: 0.188, b: 0.631 },
            { r: 0.573, g: 0.0, b: 0.349 },
            { r: 0.302, g: 0.0, b: 0.196 }
        ];

        if (concentration < 20) {
            return baseColors[0];
        } else if (concentration < 50) {
            const t = (concentration - 20) / 30;
            return this.interpolateColor(baseColors[0], baseColors[1], t);
        } else if (concentration < 100) {
            const t = (concentration - 50) / 50;
            return this.interpolateColor(baseColors[1], baseColors[2], t);
        } else {
            const t = Math.min(1, (concentration - 100) / 100);
            return this.interpolateColor(baseColors[2], baseColors[3], t);
        }
    },

    getIntensityGradient(value, max) {
        const t = Math.min(1, value / max);
        if (t < 0.25) {
            return this.interpolateColor(
                { r: 0, g: 0, b: 0.5 },
                { r: 0, g: 0.5, b: 1 },
                t / 0.25
            );
        } else if (t < 0.5) {
            return this.interpolateColor(
                { r: 0, g: 0.5, b: 1 },
                { r: 0, g: 1, b: 0.5 },
                (t - 0.25) / 0.25
            );
        } else if (t < 0.75) {
            return this.interpolateColor(
                { r: 0, g: 1, b: 0.5 },
                { r: 1, g: 1, b: 0 },
                (t - 0.5) / 0.25
            );
        } else {
            return this.interpolateColor(
                { r: 1, g: 1, b: 0 },
                { r: 1, g: 0, b: 0 },
                (t - 0.75) / 0.25
            );
        }
    }
};
