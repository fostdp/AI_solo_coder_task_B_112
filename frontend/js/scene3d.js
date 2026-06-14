class SlipScene {
    constructor(canvas) {
        this.canvas = canvas;
        this.scene = null;
        this.camera = null;
        this.renderer = null;
        this.controls = null;
        this.slipModel = null;
        this.particleSystem = null;
        this.raycaster = new THREE.Raycaster();
        this.mouse = new THREE.Vector2();
        this.slipsData = [];
        this.hoveredSlip = null;
        this.selectedSlip = null;
        this.clock = new THREE.Clock();
        this.autoRotate = false;
        this.displayMode = 'fading';
        this.showFading = true;
        this.showMold = true;
        this.showLabels = false;
        this.onSlipClick = null;
        this.onSlipHover = null;

        this.init();
    }

    init() {
        const rect = this.canvas.getBoundingClientRect();
        const width = rect.width;
        const height = rect.height;

        this.scene = new THREE.Scene();
        this.scene.background = new THREE.Color(0x0a0e1a);
        this.scene.fog = new THREE.Fog(0x0a0e1a, 15, 40);

        this.camera = new THREE.PerspectiveCamera(45, width / height, 0.1, 100);
        this.camera.position.set(8, 10, 8);

        this.renderer = new THREE.WebGLRenderer({
            canvas: this.canvas,
            antialias: true,
            alpha: true
        });
        this.renderer.setSize(width, height);
        this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
        this.renderer.shadowMap.enabled = true;
        this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;

        this.controls = new THREE.OrbitControls(this.camera, this.renderer.domElement);
        this.controls.enableDamping = true;
        this.controls.dampingFactor = 0.05;
        this.controls.minDistance = 2;
        this.controls.maxDistance = 30;
        this.controls.maxPolarAngle = Math.PI / 2.2;
        this.controls.target.set(0, 1, 0);

        this.addLights();
        this.addGround();

        this.slipModel = new SlipModel(this.scene);
        this.particleSystem = new MoldParticles(this.scene);

        this.setupEventListeners();
    }

    addLights() {
        const ambientLight = new THREE.AmbientLight(0xffffff, 0.4);
        this.scene.add(ambientLight);

        const mainLight = new THREE.DirectionalLight(0xffffff, 0.8);
        mainLight.position.set(5, 10, 5);
        mainLight.castShadow = true;
        mainLight.shadow.mapSize.width = 2048;
        mainLight.shadow.mapSize.height = 2048;
        this.scene.add(mainLight);

        const fillLight = new THREE.DirectionalLight(0x63b3ed, 0.3);
        fillLight.position.set(-5, 5, -5);
        this.scene.add(fillLight);

        const rimLight = new THREE.DirectionalLight(0x4fd1c5, 0.2);
        rimLight.position.set(0, 5, -8);
        this.scene.add(rimLight);
    }

    addGround() {
        const groundGeometry = new THREE.CircleGeometry(12, 64);
        const groundMaterial = new THREE.MeshStandardMaterial({
            color: 0x1a202c,
            roughness: 0.8,
            metalness: 0.2
        });
        const ground = new THREE.Mesh(groundGeometry, groundMaterial);
        ground.rotation.x = -Math.PI / 2;
        ground.receiveShadow = true;
        this.scene.add(ground);

        const gridHelper = new THREE.GridHelper(12, 24, 0x2d3748, 0x1a202c);
        this.scene.add(gridHelper);

        const axesHelper = new THREE.AxesHelper(2);
        this.scene.add(axesHelper);
    }

    createSlips(slipsData, statusData = []) {
        this.slipsData = slipsData;

        this.slipModel.create(slipsData, statusData);

        const statusMap = new Map(statusData.map(s => [s.slip_id, s]));
        for (const slip of slipsData) {
            const status = statusMap.get(slip.slip_id);
            if (status && status.mold_concentration > 20 && this.showMold) {
                this.particleSystem.addSlip(
                    slip.slip_id,
                    { x: slip.position_x, y: slip.position_y, z: slip.position_z },
                    status.mold_concentration
                );
            }
        }

        const overlay = document.getElementById('loadingOverlay');
        if (overlay) overlay.style.display = 'none';
    }

    updateSlipsStatus(statusData) {
        if (!this.slipModel || !this.slipModel.mesh) return;

        this.slipModel.updateStatus(statusData);

        const statusMap = new Map(statusData.map(s => [s.slip_id, s]));

        this.slipsData.forEach((slip) => {
            const status = statusMap.get(slip.slip_id);
            if (status && status.mold_concentration > 20) {
                if (this.particleSystem.slipMap.has(slip.slip_id)) {
                    this.particleSystem.updateSlip(
                        slip.slip_id,
                        { x: slip.position_x, y: slip.position_y, z: slip.position_z },
                        status.mold_concentration
                    );
                } else if (this.showMold) {
                    this.particleSystem.addSlip(
                        slip.slip_id,
                        { x: slip.position_x, y: slip.position_y, z: slip.position_z },
                        status.mold_concentration
                    );
                }
            } else {
                this.particleSystem.removeSlip(slip.slip_id);
            }
        });
    }

    setDisplayMode(mode) {
        this.displayMode = mode;
        this.slipModel.setDisplayMode(mode);
        this.updateSlipsColors();
    }

    setShowFading(show) {
        this.showFading = show;
        this.slipModel.setShowFading(show);
        this.updateSlipsColors();
    }

    setShowMold(show) {
        this.showMold = show;
        this.particleSystem.setVisible(show);
    }

    setShowLabels(show) {
        this.showLabels = show;
    }

    setAutoRotate(autoRotate) {
        this.autoRotate = autoRotate;
        this.controls.autoRotate = autoRotate;
    }

    updateSlipsColors() {
        api.getSlipsStatus().then(response => {
            if (response.success && response.data) {
                this.updateSlipsStatus(response.data);
            }
        }).catch(console.error);
    }

    setupEventListeners() {
        this.canvas.addEventListener('mousemove', (event) => {
            const rect = this.canvas.getBoundingClientRect();
            this.mouse.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
            this.mouse.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
            this.handleMouseMove();
        });

        this.canvas.addEventListener('click', () => {
            this.handleClick();
        });

        window.addEventListener('resize', () => {
            this.onResize();
        });
    }

    handleMouseMove() {
        if (!this.slipModel || !this.slipModel.mesh) return;

        this.raycaster.setFromCamera(this.mouse, this.camera);
        const instanceId = this.slipModel.getInstanceIdAt(this.raycaster);

        if (instanceId !== null) {
            if (instanceId !== this.hoveredSlip) {
                this.hoveredSlip = instanceId;
                const slip = this.slipModel.getSlipAt(instanceId);
                this.canvas.style.cursor = 'pointer';
                if (this.onSlipHover && slip) {
                    this.onSlipHover(slip);
                }
            }
        } else {
            if (this.hoveredSlip !== null) {
                this.hoveredSlip = null;
                this.canvas.style.cursor = 'grab';
            }
        }
    }

    handleClick() {
        if (!this.slipModel || this.hoveredSlip === null) return;

        const slip = this.slipModel.getSlipAt(this.hoveredSlip);
        this.selectedSlip = this.hoveredSlip;

        if (this.onSlipClick && slip) {
            this.onSlipClick(slip);
        }
    }

    onResize() {
        const rect = this.canvas.getBoundingClientRect();
        const width = rect.width;
        const height = rect.height;

        this.camera.aspect = width / height;
        this.camera.updateProjectionMatrix();
        this.renderer.setSize(width, height);
    }

    animate() {
        const delta = this.clock.getDelta();
        const time = this.clock.getElapsedTime();

        this.controls.update();
        this.particleSystem.update(time);
        this.renderer.render(this.scene, this.camera);

        requestAnimationFrame(() => this.animate());
    }

    dispose() {
        if (this.particleSystem) {
            this.particleSystem.dispose();
        }
        if (this.slipModel) {
            this.slipModel.dispose();
        }
        if (this.renderer) {
            this.renderer.dispose();
        }
    }
}
