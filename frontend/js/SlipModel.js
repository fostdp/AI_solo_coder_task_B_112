class SlipModel {
    constructor(scene, maxCount = 5000) {
        this.scene = scene;
        this.maxCount = maxCount;
        this.mesh = null;
        this.slipsData = [];
        this.slipColors = null;
        this.dummy = new THREE.Object3D();
        this.displayMode = 'fading';
        this.showFading = true;
        this.showMold = true;
        this.riskThresholds = {
            fading: { low: 5, medium: 10, high: 20 },
            mold: { low: 40, medium: 70, high: 100 }
        };
    }

    create(slipsData, statusData = []) {
        this.slipsData = slipsData;

        if (this.mesh) {
            this.scene.remove(this.mesh);
            this.mesh.geometry.dispose();
            this.mesh.material.dispose();
        }

        const geo = new THREE.BoxGeometry(0.02, 0.27, 0.015);
        const mat = new THREE.MeshStandardMaterial({
            color: 0x8B7355,
            roughness: 0.7,
            metalness: 0.1
        });

        this.mesh = new THREE.InstancedMesh(geo, mat, slipsData.length);
        this.mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
        this.mesh.castShadow = true;
        this.mesh.receiveShadow = true;

        this.slipColors = new Float32Array(slipsData.length * 3);
        this.mesh.instanceColor = new THREE.InstancedBufferAttribute(this.slipColors, 3);
        this.mesh.instanceColor.setUsage(THREE.DynamicDrawUsage);

        const statusMap = new Map(statusData.map(s => [s.slip_id, s]));

        for (let i = 0; i < slipsData.length; i++) {
            const slip = slipsData[i];
            this.dummy.position.set(slip.position_x, slip.position_y, slip.position_z);
            this.dummy.rotation.set(
                (Math.random() - 0.5) * 0.05,
                Math.random() * Math.PI * 2,
                (Math.random() - 0.5) * 0.05
            );
            this.dummy.scale.set(1, 1, slip.length / 0.27);
            this.dummy.updateMatrix();
            this.mesh.setMatrixAt(i, this.dummy.matrix);

            const status = statusMap.get(slip.slip_id);
            const color = this.computeColor(slip, status);
            this.slipColors[i * 3] = color.r;
            this.slipColors[i * 3 + 1] = color.g;
            this.slipColors[i * 3 + 2] = color.b;
        }

        this.mesh.instanceColor.needsUpdate = true;
        this.mesh.instanceMatrix.needsUpdate = true;
        this.scene.add(this.mesh);

        return this;
    }

    updateStatus(statusData) {
        if (!this.mesh || !this.slipColors) return;

        const statusMap = new Map(statusData.map(s => [s.slip_id, s]));

        for (let i = 0; i < this.slipsData.length; i++) {
            const slip = this.slipsData[i];
            const status = statusMap.get(slip.slip_id);
            const color = this.computeColor(slip, status);
            this.slipColors[i * 3] = color.r;
            this.slipColors[i * 3 + 1] = color.g;
            this.slipColors[i * 3 + 2] = color.b;
        }

        this.mesh.instanceColor.needsUpdate = true;
    }

    computeColor(slip, status) {
        if (!status || !this.showFading) {
            return { r: 0.545, g: 0.451, b: 0.333 };
        }

        switch (this.displayMode) {
            case 'fading':
                return ColorMap.getFadingColor(status.fading_rate || 0);
            case 'mold':
                return ColorMap.getMoldColor(status.mold_concentration || 0);
            case 'composite':
                return ColorMap.getCompositeColor(
                    status.fading_rate || 0,
                    status.mold_concentration || 0
                );
            default:
                return ColorMap.getFadingColor(status.fading_rate || 0);
        }
    }

    setDisplayMode(mode) {
        this.displayMode = mode;
    }

    setShowFading(show) {
        this.showFading = show;
    }

    setShowMold(show) {
        this.showMold = show;
    }

    getSlipAt(instanceId) {
        if (instanceId >= 0 && instanceId < this.slipsData.length) {
            return this.slipsData[instanceId];
        }
        return null;
    }

    getInstanceIdAt(raycaster) {
        if (!this.mesh) return null;
        const intersects = raycaster.intersectObject(this.mesh);
        if (intersects.length > 0 && intersects[0].instanceId !== undefined) {
            return intersects[0].instanceId;
        }
        return null;
    }

    highlightSlip(instanceId) {
    }

    resetHighlight() {
    }

    dispose() {
        if (this.mesh) {
            this.scene.remove(this.mesh);
            this.mesh.geometry.dispose();
            this.mesh.material.dispose();
            this.mesh = null;
        }
        this.slipsData = [];
        this.slipColors = null;
    }
}
