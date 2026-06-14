class MoldParticleSystem {
    constructor(scene) {
        this.scene = scene;
        this.particleSystems = new Map();
        this.maxParticlesPerSlip = 50;
        this.particleSize = 0.015;
    }

    createParticleSystem(slipId, position, concentration) {
        const particleCount = Math.min(this.maxParticlesPerSlip, Math.floor(concentration / 2));
        if (particleCount < 5) return null;

        const geometry = new THREE.BufferGeometry();
        const positions = new Float32Array(particleCount * 3);
        const colors = new Float32Array(particleCount * 3);
        const sizes = new Float32Array(particleCount);
        const speeds = new Float32Array(particleCount);

        const particleColor = ColorMap.getMoldParticleColor(concentration);

        for (let i = 0; i < particleCount; i++) {
            const i3 = i * 3;
            const angle = Math.random() * Math.PI * 2;
            const radius = Math.random() * 0.08;
            const heightOffset = (Math.random() - 0.5) * 0.02;

            positions[i3] = position.x + Math.cos(angle) * radius;
            positions[i3 + 1] = position.y + heightOffset;
            positions[i3 + 2] = position.z + Math.sin(angle) * radius;

            colors[i3] = particleColor.r;
            colors[i3 + 1] = particleColor.g;
            colors[i3 + 2] = particleColor.b;

            sizes[i] = this.particleSize * (0.5 + Math.random() * 0.5);
            speeds[i] = 0.001 + Math.random() * 0.002;
        }

        geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
        geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
        geometry.setAttribute('size', new THREE.BufferAttribute(sizes, 1));

        const material = new THREE.PointsMaterial({
            size: this.particleSize,
            vertexColors: true,
            transparent: true,
            opacity: 0.7,
            sizeAttenuation: true,
            blending: THREE.AdditiveBlending,
            depthWrite: false
        });

        const particles = new THREE.Points(geometry, material);
        particles.userData = {
            slipId: slipId,
            concentration: concentration,
            speeds: speeds,
            basePositions: positions.slice(),
            timeOffsets: new Float32Array(particleCount).map(() => Math.random() * Math.PI * 2)
        };

        this.particleSystems.set(slipId, particles);
        this.scene.add(particles);

        return particles;
    }

    updateParticleSystem(slipId, concentration) {
        const system = this.particleSystems.get(slipId);
        if (!system) {
            return;
        }

        const newCount = Math.min(this.maxParticlesPerSlip, Math.floor(concentration / 2));
        const currentCount = system.geometry.attributes.position.count;

        if (newCount < 5) {
            this.removeParticleSystem(slipId);
            return;
        } else if (newCount !== currentCount) {
            const position = {
                x: system.position.x,
                y: system.position.y,
                z: system.position.z
            };
            this.removeParticleSystem(slipId);
            this.createParticleSystem(slipId, position, concentration);
        } else {
            system.userData.concentration = concentration;
            const particleColor = ColorMap.getMoldParticleColor(concentration);
            const colors = system.geometry.attributes.color.array;
            for (let i = 0; i < currentCount; i++) {
                const i3 = i * 3;
                colors[i3] = particleColor.r;
                colors[i3 + 1] = particleColor.g;
                colors[i3 + 2] = particleColor.b;
            }
            system.geometry.attributes.color.needsUpdate = true;
        }
    }

    removeParticleSystem(slipId) {
        const system = this.particleSystems.get(slipId);
        if (system) {
            this.scene.remove(system);
            system.geometry.dispose();
            system.material.dispose();
            this.particleSystems.delete(slipId);
        }
    }

    update(time) {
        this.particleSystems.forEach(system => {
            const positions = system.geometry.attributes.position.array;
            const basePositions = system.userData.basePositions;
            const speeds = system.userData.speeds;
            const timeOffsets = system.userData.timeOffsets;
            const count = positions.length / 3;

            for (let i = 0; i < count; i++) {
                const i3 = i * 3;
                const t = time * speeds[i] + timeOffsets[i];
                positions[i3] = basePositions[i3] + Math.sin(t * 2) * 0.005;
                positions[i3 + 1] = basePositions[i3 + 1] + Math.sin(t * 1.5) * 0.003;
                positions[i3 + 2] = basePositions[i3 + 2] + Math.cos(t * 1.8) * 0.005;
            }

            system.geometry.attributes.position.needsUpdate = true;

            const pulse = 0.7 + Math.sin(time * 2) * 0.2;
            system.material.opacity = pulse;
        });
    }

    setVisible(visible) {
        this.particleSystems.forEach(system => {
            system.visible = visible;
        });
    }

    dispose() {
        this.particleSystems.forEach((system, slipId => {
            this.scene.remove(system);
            system.geometry.dispose();
            system.material.dispose();
        });
        this.particleSystems.clear();
    }
}
