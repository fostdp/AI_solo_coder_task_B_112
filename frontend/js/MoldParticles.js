class MoldParticles {
    constructor(scene, maxTotal = 50000, maxPerSlip = 50) {
        this.scene = scene;
        this.maxTotalParticles = maxTotal;
        this.maxParticlesPerSlip = maxPerSlip;
        this.particleSize = 0.015;
        this.visible = true;

        this.slipMap = new Map();
        this.dummy = new THREE.Object3D();
        this.time = 0;

        this.pool = [];
        this.activeCount = 0;
        this.freeList = [];

        this.uniforms = {
            u_time: { value: 0.0 },
            u_pulse: { value: 0.75 }
        };

        this.initMesh();
    }

    initMesh() {
        const particleGeo = new THREE.SphereGeometry(1, 6, 6);

        this.material = new THREE.ShaderMaterial({
            uniforms: this.uniforms,
            vertexShader: this.vertexShader(),
            fragmentShader: this.fragmentShader(),
            transparent: true,
            blending: THREE.AdditiveBlending,
            depthWrite: false
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
        this.speeds = new Float32Array(this.maxTotalParticles);
        this.phases = new Float32Array(this.maxTotalParticles);
        this.sizes = new Float32Array(this.maxTotalParticles);
        this.slipIds = new Uint32Array(this.maxTotalParticles);

        this.colors = new Float32Array(this.maxTotalParticles * 3);
        this.mesh.instanceColor = new THREE.InstancedBufferAttribute(
            this.colors, 3
        );
        this.mesh.instanceColor.setUsage(THREE.DynamicDrawUsage);

        this.mesh.geometry.setAttribute(
            'a_speed',
            new THREE.InstancedBufferAttribute(this.speeds, 1)
        );
        this.mesh.geometry.setAttribute(
            'a_phase',
            new THREE.InstancedBufferAttribute(this.phases, 1)
        );
        this.mesh.geometry.setAttribute(
            'a_size',
            new THREE.InstancedBufferAttribute(this.sizes, 1)
        );

        this.scene.add(this.mesh);
    }

    addSlip(slipId, position, concentration) {
        if (this.slipMap.has(slipId)) {
            this.updateSlip(slipId, position, concentration);
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

            this.speeds[idx] = 0.5 + Math.random() * 1.5;
            this.phases[idx] = Math.random() * Math.PI * 2;
            this.sizes[idx] = this.particleSize * (0.5 + Math.random() * 0.5);
            this.slipIds[idx] = slipId;

            this.colors[idx * 3] = particleColor.r;
            this.colors[idx * 3 + 1] = particleColor.g;
            this.colors[idx * 3 + 2] = particleColor.b;

            this.dummy.position.set(
                this.basePositions[idx * 3],
                this.basePositions[idx * 3 + 1],
                this.basePositions[idx * 3 + 2]
            );
            this.dummy.scale.setScalar(this.sizes[idx]);
            this.dummy.updateMatrix();
            this.mesh.setMatrixAt(idx, this.dummy.matrix);
        }

        this.slipMap.set(slipId, {
            indices: indices,
            position: { ...position },
            concentration: concentration
        });

        this.updateBounds();
        this.updateAttributes();
    }

    updateSlip(slipId, position, concentration) {
        const entry = this.slipMap.get(slipId);
        if (!entry) {
            this.addSlip(slipId, position, concentration);
            return;
        }

        const desiredCount = Math.min(
            this.maxParticlesPerSlip,
            Math.max(5, Math.floor(concentration / 2))
        );

        const particleColor = ColorMap.getMoldParticleColor(concentration);

        if (desiredCount < 5) {
            this.removeSlip(slipId);
            return;
        }

        const currentCount = entry.indices.length;

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

                this.speeds[idx] = 0.5 + Math.random() * 1.5;
                this.phases[idx] = Math.random() * Math.PI * 2;
                this.sizes[idx] = this.particleSize * (0.5 + Math.random() * 0.5);
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
        this.updateAttributes();
    }

    removeSlip(slipId) {
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
        this.uniforms.u_time.value = time;
        this.uniforms.u_pulse.value = 0.65 + Math.sin(time * 2) * 0.15;

        const needsPositionUpdate = false;

        if (needsPositionUpdate) {
            this.mesh.instanceMatrix.needsUpdate = true;
        }
    }

    setVisible(visible) {
        this.visible = visible;
        this.mesh.visible = visible;
    }

    clear() {
        for (const [slipId] of this.slipMap) {
            this.removeSlip(slipId);
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

    updateAttributes() {
        if (this.mesh.geometry.attributes.a_speed) {
            this.mesh.geometry.attributes.a_speed.needsUpdate = true;
        }
        if (this.mesh.geometry.attributes.a_phase) {
            this.mesh.geometry.attributes.a_phase.needsUpdate = true;
        }
        if (this.mesh.geometry.attributes.a_size) {
            this.mesh.geometry.attributes.a_size.needsUpdate = true;
        }
    }

    vertexShader() {
        return `
            attribute float a_speed;
            attribute float a_phase;
            attribute float a_size;

            varying vec3 vColor;

            uniform float u_time;

            void main() {
                vColor = instanceColor;

                vec3 pos = position * a_size;

                float t = u_time * a_speed + a_phase;
                vec3 offset = vec3(
                    sin(t * 2.0) * 0.005,
                    sin(t * 1.5) * 0.003,
                    cos(t * 1.8) * 0.005
                );

                float pulse = 0.5 + 0.3 * sin(t * 2.5 + a_phase);
                pos *= pulse;

                vec4 worldPos = vec4(instanceMatrix[3].xyz + offset + pos, 1.0);
                gl_Position = projectionMatrix * modelViewMatrix * worldPos;
            }
        `;
    }

    fragmentShader() {
        return `
            varying vec3 vColor;

            uniform float u_pulse;

            void main() {
                float dist = length(gl_PointCoord - vec2(0.5));
                if (dist > 0.5) discard;

                float alpha = u_pulse * (1.0 - dist * 2.0);
                gl_FragColor = vec4(vColor * 1.5, alpha);
            }
        `;
    }

    dispose() {
        this.scene.remove(this.mesh);
        this.mesh.geometry.dispose();
        this.material.dispose();
        this.slipMap.clear();
        this.freeList = [];
    }
}
