class RealtimeWebSocket {
    constructor(url) {
        this.url = url;
        this.ws = null;
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 10;
        this.reconnectDelay = 3000;
        this.listeners = new Map();
        this.connected = false;
    }

    connect() {
        try {
            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            const wsUrl = this.url || `${protocol}//${window.location.host}/ws/realtime`;
            
            this.ws = new WebSocket(wsUrl);
            
            this.ws.onopen = () => {
                this.connected = true;
                this.reconnectAttempts = 0;
                this.notifyListeners('open', {});
                this.notifyListeners('status', { connected: true });
            };

            this.ws.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    this.notifyListeners(data.type || 'message', data);
                } catch (e) {
                    this.notifyListeners('raw', event.data);
                }
            };

            this.ws.onerror = (error) => {
                this.notifyListeners('error', error);
                this.notifyListeners('status', { connected: false, error: true });
            };

            this.ws.onclose = (event) => {
                this.connected = false;
                this.notifyListeners('close', { code: event.code, reason: event.reason });
                this.notifyListeners('status', { connected: false });
                this.attemptReconnect();
            };
        } catch (e) {
            this.notifyListeners('error', e);
            this.attemptReconnect();
        }
    }

    attemptReconnect() {
        if (this.reconnectAttempts < this.maxReconnectAttempts) {
            this.reconnectAttempts++;
            const delay = this.reconnectDelay * Math.min(this.reconnectAttempts, 5);
            setTimeout(() => {
                this.connect();
            }, delay);
            this.notifyListeners('reconnecting', { attempt: this.reconnectAttempts, delay });
        }
    }

    send(data) {
        if (this.connected && this.ws) {
            if (typeof data === 'object') {
                this.ws.send(JSON.stringify(data));
            } else {
                this.ws.send(data);
            }
            return true;
        }
        return false;
    }

    on(event, callback) {
        if (!this.listeners.has(event)) {
            this.listeners.set(event, []);
        }
        this.listeners.get(event).push(callback);
    }

    off(event, callback) {
        if (this.listeners.has(event)) {
            const callbacks = this.listeners.get(event);
            const idx = callbacks.indexOf(callback);
            if (idx !== -1) {
                callbacks.splice(idx, 1);
            }
        }
    }

    notifyListeners(event, data) {
        if (this.listeners.has(event)) {
            this.listeners.get(event).forEach(callback => {
                try {
                    callback(data);
                } catch (e) {
                    console.error('WebSocket listener error:', e);
                }
            });
        }
    }

    close() {
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
        this.connected = false;
    }

    isConnected() {
        return this.connected;
    }
}

class ApiClient {
    constructor(baseUrl = '') {
        this.baseUrl = baseUrl;
    }

    async request(endpoint, options = {}) {
        const url = this.baseUrl + endpoint;
        const response = await fetch(url, {
            headers: {
                'Content-Type': 'application/json',
                ...options.headers
            },
            ...options
        });

        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }

        return response.json();
    }

    get(endpoint, params = {}) {
        const query = new URLSearchParams(params).toString();
        const url = query ? `${endpoint}?${query}` : endpoint;
        return this.request(url, { method: 'GET' });
    }

    post(endpoint, data = {}) {
        return this.request(endpoint, {
            method: 'POST',
            body: JSON.stringify(data)
        });
    }

    async getSlips(params = {}) {
        return this.get('/api/slips', params);
    }

    async getSlipById(slipId) {
        return this.get(`/api/slips/${slipId}`);
    }

    async getSlipSpectral(slipId, params = {}) {
        return this.get(`/api/slips/${slipId}/spectral`, params);
    }

    async getSlipMicrobial(slipId, params = {}) {
        return this.get(`/api/slips/${slipId}/microbial`, params);
    }

    async getSlipFading(slipId, params = {}) {
        return this.get(`/api/slips/${slipId}/fading`, params);
    }

    async getSlipMold(slipId, params = {}) {
        return this.get(`/api/slips/${slipId}/mold`, params);
    }

    async getAlerts(params = {}) {
        return this.get('/api/alerts', params);
    }

    async acknowledgeAlert(alertId, data = {}) {
        return this.post(`/api/alerts/${alertId}/acknowledge`, data);
    }

    async resolveAlert(alertId) {
        return this.post(`/api/alerts/${alertId}/resolve`, {});
    }

    async getDevices() {
        return this.get('/api/devices');
    }

    async getDashboardStats() {
        return this.get('/api/dashboard/stats');
    }

    async getSlipsStatus() {
        return this.get('/api/slips/status');
    }

    async ingestSpectralData(data) {
        return this.post('/api/v1/ingest/spectral', data);
    }

    async ingestMicrobialData(data) {
        return this.post('/api/v1/ingest/microbial', data);
    }
}

const api = new ApiClient();
const ws = new RealtimeWebSocket();
