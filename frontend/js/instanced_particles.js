class InstancedMoldParticles {
    constructor(scene) {
        this.scene = scene;
        this.maxTotalParticles = 50000;
        this.maxParticlesPerSlip = 50;
        this.particleSize = 0.015;
        this.visible = true;

        this.slipMap = new Map();
        this.dummy = new THREE.Object3D();
        this.color = new THREE.Color();
        this.time = 0;

        this.pool = [];
        this.activeCount = 0;
        this.freeList = [];

        this.initMesh();
    }

    initMesh() {
        const particleGeo = new THREE.SphereGeometry(1, 8, 8);

        this.material = new THREE.MeshBasicMaterial({
            vertexColors: true,
            transparent: true,
            opacity: 0.75,
            blending: THREE.AdditiveBlending,
            depthWrite: false,
            sizeAttenuation: true
        });

        this.mesh = new THREE.InstancedMesh(
            particleGeo,
            this.material,
            this.maxTotalParticles
        );
        this.mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
        this.mesh.count = 0;
        this.mesh.frustumCulled = false;

        this.basePositions = new Float32Array(this.maxTotalParticles * 3);
        this.offsets = new Float32Array(this.maxTotalParticles * 3);
        this.speeds = new Float32Array(this.maxTotalParticles);
        this.phases = new Float32Array(this.maxTotalParticles);
        this.slipIds = new Uint32Array(this.maxTotalParticles);

        this.colors = new Float32Array(this.maxTotalParticles * 3);
        this.mesh.instanceColor = new THREE.InstancedBufferAttribute(
            this.colors, 3
        );
        this.mesh.instanceColor.setUsage(THREE.DynamicDrawUsage);

        this.scene.add(this.mesh);
    }

    addSlipParticles(slipId, position, concentration) {
        if (this.slipMap.has(slipId)) {
            this.updateSlipParticles(slipId, position, concentration);
            return;
        }

        const count = Math.min(
            this.maxParticlesPerSlip,
            Math.max(5, Math.floor(concentration / 2))
        );
        const available = this.maxTotalParticles - this.activeCount;
        if (count > available) return;

        const indices = [];
        const particleColor = ColorMap.getMoldParticleColor(concentration);

        for (let i = 0; i < count; i++) {
            let idx;
            if (this.freeList.length > 0) {
                idx = this.freeList.pop();
            } else {
                idx = this.activeCount;
                this.activeCount++;
            }

            indices.push(idx);

            const angle = Math.random() * Math.PI * 2;
            const radius = Math.random() * 0.08;
            const heightOffset = (Math.random() - 0.5) * 0.02;

            this.basePositions[idx * 3] = position.x + Math.cos(angle) * radius;
            this.basePositions[idx * 3 + 1] = position.y + heightOffset;
            this.basePositions[idx * 3 + 2] = position.z + Math.sin(angle) * radius;

            this.offsets[idx * 3] = 0;
            this.offsets[idx * 3 + 1] = 0;
            this.offsets[idx * 3 + 2] = 0;

            this.speeds[idx] = 0.001 + Math.random() * 0.002;
            this.phases[idx] = Math.random() * Math.PI * 2;
            this.slipIds[idx] = slipId;

            this.colors[idx * 3] = particleColor.r;
            this.colors[idx * 3 + 1] = particleColor.g;
            this.colors[idx * 3 + 2] = particleColor.b;

            const size = this.particleSize * (0.5 + Math.random() * 0.5);
            this.dummy.position.set(
                this.basePositions[idx * 3],
                this.basePositions[idx * 3 + 1],
                this.basePositions[idx * 3 + 2]
            );
            this.dummy.scale.setScalar(size);
            this.dummy.updateMatrix();
            this.mesh.setMatrixAt(idx, this.dummy.matrix);
        }

        this.slipMap.set(slipId, {
            indices: indices,
            position: { ...position },
            concentration: concentration
        });

        this.updateBounds();
    }

    updateSlipParticles(slipId, position, concentration) {
        const entry = this.slipMap.get(slipId);
        if (!entry) {
            this.addSlipParticles(slipId, position, concentration);
            return;
        }

        const desiredCount = Math.min(
            this.maxParticlesPerSlip,
            Math.max(5, Math.floor(concentration / 2))
        );
        const currentCount = entry.indices.length;

        const particleColor = ColorMap.getMoldParticleColor(concentration);

        if (desiredCount < 5) {
            this.removeSlipParticles(slipId);
            return;
        }

        if (desiredCount < currentCount) {
            const removeCount = currentCount - desiredCount;
            for (let i = 0; i < removeCount; i++) {
                const idx = entry.indices.pop();
                this.freeList.push(idx);
                this.slipIds[idx] = 0;
            }
        } else if (desiredCount > currentCount) {
            const addCount = Math.min(
                desiredCount - currentCount,
                this.maxTotalParticles - this.activeCount
            );
            for (let i = 0; i < addCount; i++) {
                let idx;
                if (this.freeList.length > 0) {
                    idx = this.freeList.pop();
                } else {
                    idx = this.activeCount;
                    this.activeCount++;
                }
                entry.indices.push(idx);

                const angle = Math.random() * Math.PI * 2;
                const radius = Math.random() * 0.08;
                const heightOffset = (Math.random() - 0.5) * 0.02;

                this.basePositions[idx * 3] = position.x + Math.cos(angle) * radius;
                this.basePositions[idx * 3 + 1] = position.y + heightOffset;
                this.basePositions[idx * 3 + 2] = position.z + Math.sin(angle) * radius;

                this.speeds[idx] = 0.001 + Math.random() * 0.002;
                this.phases[idx] = Math.random() * Math.PI * 2;
                this.slipIds[idx] = slipId;
            }
        }

        for (const idx of entry.indices) {
            this.colors[idx * 3] = particleColor.r;
            this.colors[idx * 3 + 1] = particleColor.g;
            this.colors[idx * 3 + 2] = particleColor.b;
        }

        entry.concentration = concentration;
        entry.position = { ...position };

        this.mesh.instanceColor.needsUpdate = true;
        this.updateBounds();
    }

    removeSlipParticles(slipId) {
        const entry = this.slipMap.get(slipId);
        if (!entry) return;

        for (const idx of entry.indices) {
            this.freeList.push(idx);
            this.slipIds[idx] = 0;

            this.dummy.position.set(0, -1000, 0);
            this.dummy.scale.setScalar(0);
            this.dummy.updateMatrix();
            this.mesh.setMatrixAt(idx, this.dummy.matrix);
        }

        this.slipMap.delete(slipId);
        this.updateBounds();
    }

    update(time) {
        if (!this.visible || this.activeCount === 0) return;

        this.time = time;

        for (const [slipId, entry] of this.slipMap) {
            for (const idx of entry.indices) {
                const t = time * this.speeds[idx] + this.phases[idx];
                const dx = Math.sin(t * 2) * 0.005;
                const dy = Math.sin(t * 1.5) * 0.003;
                const dz = Math.cos(t * 1.8) * 0.005;

                const x = this.basePositions[idx * 3] + dx;
                const y = this.basePositions[idx * 3 + 1] + dy;
                const z = this.basePositions[idx * 3 + 2] + dz;

                const baseSize = this.particleSize *
                    (0.5 + 0.3 * Math.sin(t * 2.5 + this.phases[idx]));

                this.dummy.position.set(x, y, z);
                this.dummy.scale.setScalar(baseSize);
                this.dummy.updateMatrix();
                this.mesh.setMatrixAt(idx, this.dummy.matrix);
            }
        }

        this.mesh.instanceMatrix.needsUpdate = true;

        const pulse = 0.65 + Math.sin(time * 2) * 0.15;
        this.material.opacity = pulse;
    }

    setVisible(visible) {
        this.visible = visible;
        this.mesh.visible = visible;
    }

    clear() {
        for (const [slipId] of this.slipMap) {
            this.removeSlipParticles(slipId);
        }
    }

    getActiveParticleCount() {
        return this.activeCount - this.freeList.length;
    }

    getSlipCount() {
        return this.slipMap.size;
    }

    updateBounds() {
        let maxIdx = 0;
        for (const entry of this.slipMap.values()) {
            for (const idx of entry.indices) {
                maxIdx = Math.max(maxIdx, idx);
            }
        }
        this.mesh.count = maxIdx + 1;
        this.mesh.instanceMatrix.needsUpdate = true;
    }

    dispose() {
        this.scene.remove(this.mesh);
        this.mesh.geometry.dispose();
        this.material.dispose();
        this.slipMap.clear();
        this.freeList = [];
    }
}
