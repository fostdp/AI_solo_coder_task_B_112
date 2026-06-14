class MatchButton {
    constructor(options = {}) {
        this.container = options.container || document.body;
        this.label = options.label || '智能缀合匹配';
        this.onMatch = options.onMatch || (() => {});
        this.isLoading = false;
        this.matches = [];
        this.init();
    }

    init() {
        this.el = document.createElement('div');
        this.el.className = 'match-button-wrapper';

        this.button = document.createElement('button');
        this.button.className = 'btn btn-match';
        this.button.innerHTML = `
            <span class="match-icon">🔗</span>
            <span class="match-label">${this.label}</span>
            <span class="match-spinner" style="display:none;">⏳</span>
        `;

        this.resultsPanel = document.createElement('div');
        this.resultsPanel.className = 'match-results-panel';
        this.resultsPanel.style.display = 'none';

        this.button.addEventListener('click', () => this.handleClick());

        this.el.appendChild(this.button);
        this.el.appendChild(this.resultsPanel);
        this.container.appendChild(this.el);
    }

    async handleClick() {
        if (this.isLoading) return;
        this.setLoading(true);
        try {
            this.matches = await this.onMatch();
            this.renderResults();
        } finally {
            this.setLoading(false);
        }
    }

    setLoading(state) {
        this.isLoading = state;
        this.button.disabled = state;
        this.button.classList.toggle('loading', state);
        const spinner = this.button.querySelector('.match-spinner');
        if (spinner) spinner.style.display = state ? 'inline' : 'none';
    }

    renderResults() {
        if (!this.matches || this.matches.length === 0) {
            this.resultsPanel.innerHTML = '<p class="no-matches">未找到匹配的缀合简牍</p>';
            this.resultsPanel.style.display = 'block';
            return;
        }

        const list = this.matches
            .sort((a, b) => (b.score || 0) - (a.score || 0))
            .slice(0, 10)
            .map(m => {
                const pct = Math.round((m.score || 0) * 100);
                const level = pct >= 90 ? 'excellent' : pct >= 75 ? 'good' : pct >= 60 ? 'fair' : 'poor';
                return `
                    <div class="match-item ${level}">
                        <div class="match-id">#${m.slip_id || '?'}</div>
                        <div class="match-score-bar">
                            <div class="match-score-fill" style="width:${pct}%"></div>
                        </div>
                        <div class="match-score">${pct}%</div>
                    </div>
                `;
            }).join('');

        this.resultsPanel.innerHTML = `
            <h4>缀合推荐结果 (${this.matches.length})</h4>
            <div class="match-list">${list}</div>
        `;
        this.resultsPanel.style.display = 'block';
    }

    hide() { this.resultsPanel.style.display = 'none'; }
    show() { this.resultsPanel.style.display = 'block'; }

    destroy() {
        this.button.removeEventListener('click', this.handleClick);
        this.el.remove();
    }
}

if (typeof window !== 'undefined') {
    window.MatchButton = MatchButton;
}
