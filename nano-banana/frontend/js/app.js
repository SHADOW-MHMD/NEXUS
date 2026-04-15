// NEXUS Inventory Manager - Frontend Application

const API_BASE = `${window.location.origin}/api`;
let currentUser = null;
let currentTab = 'dashboard';

// ============================================================================
// INITIALIZATION
// ============================================================================

document.addEventListener('DOMContentLoaded', function() {
    // Login form handler
    document.getElementById('login-form').addEventListener('submit', handleLogin);
    
    // Item form handler
    document.getElementById('item-form').addEventListener('submit', handleAddItem);
    
    // User form handler
    document.getElementById('user-form').addEventListener('submit', handleAddUser);
    
    // Issue form handler
    document.getElementById('issue-form').addEventListener('submit', handleCreateIssue);

    // AI chat handler
    document.getElementById('ai-chat-form').addEventListener('submit', handleAiChat);
    
    // Check if user is logged in
    const token = localStorage.getItem('auth_token');
    if (token) {
        const user = JSON.parse(localStorage.getItem('current_user'));
        if (user) {
            loginSuccess(user, token);
        }
    }
});

// ============================================================================
// AUTHENTICATION
// ============================================================================

async function handleLogin(e) {
    e.preventDefault();
    
    const username = document.getElementById('login-username').value;
    const password = document.getElementById('login-password').value;
    
    try {
        const response = await fetch(`${API_BASE}/auth/login`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                username: username,
                password: password
            })
        });
        
        const data = await response.json();
        
        if (data.error) {
            showLoginError(data.error);
            return;
        }
        
        // Store token and user info
        localStorage.setItem('auth_token', data.token);
        localStorage.setItem('current_user', JSON.stringify(data));
        
        loginSuccess(data, data.token);
    } catch (error) {
        showLoginError('Connection error: ' + error.message);
    }
}

function loginSuccess(user, token) {
    currentUser = user;
    document.getElementById('login-screen').classList.add('hidden');
    document.getElementById('dashboard-screen').classList.remove('hidden');
    
    // Load dashboard data
    loadDashboard();
    loadItems();
    loadUsers();
    loadIssues();
    loadReports();
}

function logout() {
    currentUser = null;
    localStorage.removeItem('auth_token');
    localStorage.removeItem('current_user');
    
    document.getElementById('dashboard-screen').classList.add('hidden');
    document.getElementById('login-screen').classList.remove('hidden');
    
    // Clear forms
    document.getElementById('login-form').reset();
    document.getElementById('login-username').focus();
}

function showLoginError(message) {
    const errorDiv = document.getElementById('login-error');
    errorDiv.textContent = message;
    errorDiv.classList.add('show');
    setTimeout(() => errorDiv.classList.remove('show'), 4000);
}

// ============================================================================
// NAVIGATION
// ============================================================================

function switchTab(tabName) {
    // Hide all tabs
    document.querySelectorAll('.tab-content').forEach(tab => {
        tab.classList.add('hidden');
    });
    
    // Show selected tab
    const tab = document.getElementById(tabName);
    if (tab) {
        tab.classList.remove('hidden');
        currentTab = tabName;
        
        // Load data when specific tabs are opened
        if (tabName === 'reports') {
            loadReports();
        }
        if (tabName === 'users') {
            loadUsers();
        }
    }
    
    // Close sidebar on mobile after clicking a tab
    if (window.innerWidth <= 768) {
        const sidebar = document.getElementById('sidebar');
        const overlay = document.getElementById('sidebar-overlay');
        sidebar.classList.add('closed');
        sidebar.classList.remove('open');
        overlay.classList.remove('active');
    }
}

function toggleSidebar() {
    const sidebar = document.getElementById('sidebar');
    const overlay = document.getElementById('sidebar-overlay');
    
    sidebar.classList.toggle('closed');
    sidebar.classList.toggle('open');
    overlay.classList.toggle('active');
}

// ============================================================================
// DIALOGS
// ============================================================================

function openAddItemDialog() {
    document.getElementById('item-form').reset();
    document.getElementById('item-dialog').classList.remove('hidden');
}

function openAddUserDialog() {
    document.getElementById('user-form').reset();
    document.getElementById('user-dialog').classList.remove('hidden');
}

function openCreateIssueDialog() {
    document.getElementById('issue-form').reset();
    document.getElementById('issue-dialog').classList.remove('hidden');
    const issueDateInput = document.getElementById('issue-date');
    if (issueDateInput) {
        issueDateInput.value = new Date().toISOString().split('T')[0];
    }
    
    // Populate items dropdown
    const items = JSON.parse(localStorage.getItem('items_cache') || '[]');
    const select = document.getElementById('issue-item');
    select.innerHTML = '';
    items.forEach(item => {
        const option = document.createElement('option');
        option.value = item.id;
        option.textContent = `${item.sku} - ${item.name}`;
        select.appendChild(option);
    });
}

function closeDialog(dialogId) {
    document.getElementById(dialogId).classList.add('hidden');
}

// Click outside modal to close
window.onclick = function(event) {
    if (event.target.classList.contains('modal')) {
        event.target.classList.add('hidden');
    }
}

// ============================================================================
// DASHBOARD
// ============================================================================

async function loadDashboard() {
    try {
        // Load statistics
        const itemsData = JSON.parse(localStorage.getItem('items_cache') || '[]');
        const usersData = JSON.parse(localStorage.getItem('users_cache') || '[]');
        const issuesData = JSON.parse(localStorage.getItem('issues_cache') || '[]');
        
        document.getElementById('total-items').textContent = itemsData.length;
        document.getElementById('total-users').textContent = usersData.length;
        document.getElementById('active-issues').textContent = issuesData.filter(i => i.status !== 'RETURNED').length;
        
        // Count low stock items
        const lowStock = itemsData.filter(i => i.quantity <= i.min_quantity || i.min_quantity === undefined).length;
        document.getElementById('low-stock-items').textContent = lowStock;

        await loadAiAlerts();
    } catch (error) {
        console.error('Error loading dashboard:', error);
    }
}

async function loadAiAlerts() {
    try {
        const response = await fetch(`${API_BASE}/ai/alerts`, {
            headers: {
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            }
        });

        let alerts = await response.json();
        if (!Array.isArray(alerts)) alerts = [];

        const list = document.getElementById('ai-alerts-list');
        const modelBadge = document.getElementById('ai-alerts-model');
        list.innerHTML = '';

        if (alerts.length === 0) {
            list.innerHTML = '<li class="font-black uppercase text-peel/30">No active alerts. The system looks stable.</li>';
            return;
        }

        alerts.forEach(alert => {
            const item = document.createElement('li');
            item.className = 'flex items-start gap-3 border-2 border-peel rounded-banana p-4';
            item.innerHTML = `
                <span class="material-symbols-outlined font-black mt-1">${alert.icon || 'info'}</span>
                <div>
                    <p class="font-black uppercase text-xs text-peel/50">${(alert.type || 'alert').replace('_', ' ')}</p>
                    <p class="font-bold">${alert.message}</p>
                </div>
            `;
            list.appendChild(item);
        });
    } catch (error) {
        console.error('Error loading AI alerts:', error);
    }
}

async function loadAiInsights() {
    const content = document.getElementById('ai-insights-content');
    if (!content) return;

    content.textContent = 'Generating insights...';

    try {
        // Fetch items and issues to send to the analyze endpoint
        const items = JSON.parse(localStorage.getItem('items_cache') || '[]');
        const issues = JSON.parse(localStorage.getItem('issues_cache') || '[]');

        const response = await fetch(`${API_BASE}/ai/alerts`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            },
            body: JSON.stringify({ items, issues })
        });

        const data = await response.json();

        // Build insights summary
        const lowStock = (items || []).filter(i => (i.quantity || 0) < 5);
        const damaged = (items || []).filter(i => i.condition === 'Damaged');
        const overdue = (issues || []).filter(i => i.status === 'OVERDUE');

        let insights = '';
        if (lowStock.length > 0) {
            insights += `⚠ LOW STOCK ALERT\n${lowStock.map(i => `  • ${i.name} (qty: ${i.quantity}, min: ${i.min_quantity || 0})`).join('\n')}\n\n`;
        }
        if (damaged.length > 0) {
            insights += `🔧 DAMAGED ITEMS\n${damaged.map(i => `  • ${i.name} (SKU: ${i.sku})`).join('\n')}\n\n`;
        }
        if (overdue.length > 0) {
            insights += `⏰ OVERDUE ISSUES\n${overdue.map(i => `  • Item #${i.item_id} → ${i.issued_to || i.assignee} (due: ${i.expected_return_date || 'N/A'})`).join('\n')}\n\n`;
        }

        if (!insights) {
            insights = '✓ All clear — no low stock, damaged, or overdue issues detected.';
        }

        content.textContent = insights;
    } catch (error) {
        content.textContent = 'Failed to load insights. The inventory data may be incomplete.';
        console.error('Error loading AI insights:', error);
    }
}

function getConditionClass(condition) {
    if (condition === 'Damaged') return 'bg-bruise text-white';
    if (condition === 'Under Repair') return 'bg-orange-400 text-peel';
    return 'bg-stem text-white';
}

function getConditionSelectMarkup(item) {
    const conditions = ['Working', 'Damaged', 'Under Repair'];
    const options = conditions.map(condition =>
        `<option value="${condition}" ${item.condition === condition ? 'selected' : ''}>${condition}</option>`
    ).join('');

    return `
        <select class="brutalist-input !py-2 !px-3 text-sm font-black ${getConditionClass(item.condition)}"
                onchange="updateItemCondition(${item.id}, this.value)">
            ${options}
        </select>
    `;
}

// ============================================================================
// ITEMS MANAGEMENT
// ============================================================================

async function loadItems() {
    try {
        const response = await fetch(`${API_BASE}/items`, {
            headers: {
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            }
        });
        
        let items = await response.json();
        if (!Array.isArray(items)) items = [];
        
        // Cache items
        localStorage.setItem('items_cache', JSON.stringify(items));
        
        // Populate table
        const tbody = document.getElementById('inventory-tbody');
        tbody.innerHTML = '';

        if (items.length === 0) {
            tbody.innerHTML = '<tr><td colspan="6" class="py-20 text-center font-black uppercase text-peel/20">Bro ur inventry is empty go buy something and update it</td></tr>';
            return;
        }
        
        items.forEach(item => {
            const isLowStock = item.quantity <= (item.min_quantity || 5);
            const row = document.createElement('tr');
            row.innerHTML = `
                <td class="font-black text-peel/40">${item.sku}</td>
                <td class="font-black">${item.name}</td>
                <td>${getConditionSelectMarkup(item)}</td>
                <td class="text-center">
                    <span class="qty-badge ${isLowStock ? 'qty-low' : ''}">${item.quantity}</span>
                </td>
                <td class="font-medium">${item.location || 'Not Set'}</td>
                <td class="text-right">
                    <div class="flex justify-end gap-2">
                        <button class="action-icon-btn" onclick="editItem(${item.id})">
                            <span class="material-symbols-outlined">edit</span>
                        </button>
                        <button class="action-icon-btn delete" onclick="deleteItem(${item.id})">
                            <span class="material-symbols-outlined">delete</span>
                        </button>
                    </div>
                </td>
            `;
            tbody.appendChild(row);
        });
    } catch (error) {
        console.error('Error loading items:', error);
    }
}

async function handleAddItem(e) {
    e.preventDefault();
    
    const sku = document.getElementById('item-sku').value;
    const name = document.getElementById('item-name').value;
    const description = document.getElementById('item-description').value;
    const condition = document.getElementById('item-condition').value;
    const quantity = parseInt(document.getElementById('item-quantity').value);
    
    try {
        const response = await fetch(`${API_BASE}/items`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            },
            body: `sku=${encodeURIComponent(sku)}&name=${encodeURIComponent(name)}&description=${encodeURIComponent(description)}&condition=${encodeURIComponent(condition)}&quantity=${quantity}`
        });
        
        const data = await response.json();
        
        if (data.status === 'success') {
            closeDialog('item-dialog');
            await loadItems();
            loadDashboard();
            showMessage('Item added successfully!');
        } else {
            showMessage('Error: ' + (data.error || 'Failed to add item'), 'error');
        }
    } catch (error) {
        showMessage('Error: ' + error.message, 'error');
    }
}

function searchItems() {
    const search = document.getElementById('inventory-search').value;
    const items = JSON.parse(localStorage.getItem('items_cache') || '[]');
    
    const filtered = items.filter(item =>
        item.name.toLowerCase().includes(search.toLowerCase()) ||
        item.sku.toLowerCase().includes(search.toLowerCase())
    );
    
    const tbody = document.getElementById('inventory-tbody');
    tbody.innerHTML = '';

    if (filtered.length === 0) {
        tbody.innerHTML = '<tr><td colspan="6" class="py-20 text-center font-black uppercase text-peel/20">Bro ur inventry is empty go buy something and update it</td></tr>';
        return;
    }
    
    filtered.forEach(item => {
        const isLowStock = item.quantity <= (item.min_quantity || 5);
        const row = document.createElement('tr');
        row.innerHTML = `
            <td class="font-black text-peel/40">${item.sku}</td>
            <td class="font-black">${item.name}</td>
            <td>${getConditionSelectMarkup(item)}</td>
            <td class="text-center">
                <span class="qty-badge ${isLowStock ? 'qty-low' : ''}">${item.quantity}</span>
            </td>
            <td class="font-medium">${item.location || 'Not Set'}</td>
            <td class="text-right">
                <div class="flex justify-end gap-2">
                    <button class="action-icon-btn" onclick="editItem(${item.id})">
                        <span class="material-symbols-outlined">edit</span>
                    </button>
                    <button class="action-icon-btn delete" onclick="deleteItem(${item.id})">
                        <span class="material-symbols-outlined">delete</span>
                    </button>
                </div>
            </td>
        `;
        tbody.appendChild(row);
    });
}

function editItem(itemId) {
    const items = JSON.parse(localStorage.getItem('items_cache') || '[]');
    const item = items.find(i => i.id === itemId);

    if (!item) {
        showMessage('Item not found', 'error');
        return;
    }

    const name = prompt('Edit item name:', item.name);
    if (name === null) return;

    const description = prompt('Edit item description:', item.description || '');
    if (description === null) return;

    const condition = prompt('Edit item condition (Working, Damaged, Under Repair):', item.condition || 'Working');
    if (condition === null) return;

    const quantity = prompt('Edit item quantity:', item.quantity);
    if (quantity === null) return;

    const qtyInt = parseInt(quantity, 10);
    if (Number.isNaN(qtyInt) || qtyInt < 0) {
        showMessage('Please enter a valid quantity', 'error');
        return;
    }

    updateItemRequest(itemId, item.sku, name, description, qtyInt, condition);
}

async function updateItemRequest(itemId, sku, name, description, quantity, condition) {
    try {
        const response = await fetch(`${API_BASE}/items/${itemId}`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            },
            body: `sku=${encodeURIComponent(sku)}&name=${encodeURIComponent(name)}&description=${encodeURIComponent(description)}&condition=${encodeURIComponent(condition)}&quantity=${quantity}`
        });

        const data = await response.json();

        if (data.status === 'success') {
            await loadItems();
            loadDashboard();
            showMessage('Item updated successfully!');
        } else {
            showMessage('Error: ' + (data.error || 'Failed to update item'), 'error');
        }
    } catch (error) {
        showMessage('Error: ' + error.message, 'error');
    }
}

async function updateItemCondition(itemId, condition) {
    const items = JSON.parse(localStorage.getItem('items_cache') || '[]');
    const item = items.find(i => i.id === itemId);
    if (!item) return;

    await updateItemRequest(itemId, item.sku, item.name, item.description || '', item.quantity, condition);
}

function deleteItem(itemId) {
    if (confirm('Are you sure you want to delete this item?')) {
        deleteItemRequest(itemId);
    }
}

async function deleteItemRequest(itemId) {
    try {
        const response = await fetch(`${API_BASE}/items/${itemId}`, {
            method: 'DELETE',
            headers: {
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            }
        });

        const data = await response.json();

        if (data.status === 'success') {
            await loadItems();
            loadDashboard();
            showMessage('Item deleted successfully!');
        } else {
            showMessage('Error: ' + (data.error || 'Failed to delete item'), 'error');
        }
    } catch (error) {
        showMessage('Error: ' + error.message, 'error');
    }
}

// ============================================================================
// USERS MANAGEMENT
// ============================================================================

async function loadUsers() {
    try {
        const response = await fetch(`${API_BASE}/users`, {
            headers: {
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            }
        });
        
        if (!response.ok) {
            console.error('Failed to load users:', response.status, response.statusText);
            return;
        }
        
        let users = await response.json();
        if (!Array.isArray(users)) users = [];
        
        // Cache users
        localStorage.setItem('users_cache', JSON.stringify(users));
        
        // Populate table
        const tbody = document.getElementById('users-tbody');
        tbody.innerHTML = '';
        
        if (users.length === 0) {
            tbody.innerHTML = '<tr><td colspan="5" class="py-20 text-center font-black uppercase text-peel/20">No members found</td></tr>';
            return;
        }
        
        users.forEach(user => {
            const row = document.createElement('tr');
            row.innerHTML = `
                <td class="font-black italic">@${user.username}</td>
                <td class="font-black">${user.full_name || '-'}</td>
                <td class="font-medium text-peel/60 text-sm">${user.email || '-'}</td>
                <td>
                    <span class="qty-badge ${user.role === 'admin' ? 'bg-banana' : 'bg-pulp'}">
                        ${user.role.toUpperCase()}
                    </span>
                </td>
                <td class="text-right">
                    <div class="flex justify-end gap-2">
                        <button class="action-icon-btn" onclick="editUser(${user.id})">
                            <span class="material-symbols-outlined">edit</span>
                        </button>
                        <button class="action-icon-btn delete" onclick="deleteUser(${user.id})">
                            <span class="material-symbols-outlined">delete</span>
                        </button>
                    </div>
                </td>
            `;
            tbody.appendChild(row);
        });
    } catch (error) {
        console.error('Error loading users:', error);
    }
}

async function handleAddUser(e) {
    e.preventDefault();
    
    const username = document.getElementById('user-username').value;
    const password = document.getElementById('user-password').value;
    const fullname = document.getElementById('user-fullname').value;
    const emailInput = document.getElementById('user-email');
    const email = emailInput ? emailInput.value : '';
    const role = document.getElementById('user-role').value;
    
    try {
        const response = await fetch(`${API_BASE}/users`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            },
            body: `username=${encodeURIComponent(username)}&password=${encodeURIComponent(password)}&fullname=${encodeURIComponent(fullname)}&email=${encodeURIComponent(email)}&role=${encodeURIComponent(role)}`
        });
        
        const data = await response.json();
        
        if (data.status === 'success') {
            closeDialog('user-dialog');
            await loadUsers();
            loadDashboard();
            showMessage('User created successfully!');
        } else {
            showMessage('Error: ' + (data.error || 'Failed to create user'), 'error');
        }
    } catch (error) {
        showMessage('Error: ' + error.message, 'error');
    }
}

function editUser(userId) {
    const users = JSON.parse(localStorage.getItem('users_cache') || '[]');
    const user = users.find(u => u.id === userId);

    if (!user) {
        showMessage('User not found', 'error');
        return;
    }

    const fullname = prompt('Edit full name:', user.full_name || '');
    if (fullname === null) return;

    const email = prompt('Edit email:', user.email || '');
    if (email === null) return;

    const role = prompt('Edit role (admin, manager, viewer):', user.role);
    if (role === null) return;

    const normalizedRole = role.trim().toLowerCase();
    if (!['admin', 'manager', 'viewer'].includes(normalizedRole)) {
        showMessage('Role must be admin, manager, or viewer', 'error');
        return;
    }

    updateUserRequest(userId, fullname, email, normalizedRole);
}

async function updateUserRequest(userId, fullname, email, role) {
    try {
        const response = await fetch(`${API_BASE}/users/${userId}`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            },
            body: `fullname=${encodeURIComponent(fullname)}&email=${encodeURIComponent(email)}&role=${encodeURIComponent(role)}`
        });

        const data = await response.json();

        if (data.status === 'success') {
            await loadUsers();
            loadDashboard();
            showMessage('User updated successfully!');
        } else {
            showMessage('Error: ' + (data.error || 'Failed to update user'), 'error');
        }
    } catch (error) {
        showMessage('Error: ' + error.message, 'error');
    }
}

function deleteUser(userId) {
    if (currentUser && currentUser.user_id === userId) {
        showMessage('You cannot delete the account you are currently using', 'error');
        return;
    }

    if (confirm('Are you sure you want to delete this user?')) {
        deleteUserRequest(userId);
    }
}

async function deleteUserRequest(userId) {
    try {
        const response = await fetch(`${API_BASE}/users/${userId}`, {
            method: 'DELETE',
            headers: {
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            }
        });

        const data = await response.json();

        if (data.status === 'success') {
            await loadUsers();
            loadDashboard();
            showMessage('User deleted successfully!');
        } else {
            showMessage('Error: ' + (data.error || 'Failed to delete user'), 'error');
        }
    } catch (error) {
        showMessage('Error: ' + error.message, 'error');
    }
}

// ============================================================================
// ISSUES MANAGEMENT
// ============================================================================

async function loadIssues() {
    try {
        const response = await fetch(`${API_BASE}/issues`, {
            headers: {
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            }
        });
        
        let issues = await response.json();
        if (!Array.isArray(issues)) issues = [];
        
        // Cache issues
        localStorage.setItem('issues_cache', JSON.stringify(issues));
        
        // Get items for reference
        const items = JSON.parse(localStorage.getItem('items_cache') || '[]');
        
        // Populate table
        const tbody = document.getElementById('issues-tbody');
        tbody.innerHTML = '';
        
        if (issues.length === 0) {
            tbody.innerHTML = '<tr><td colspan="6" class="py-20 text-center font-black uppercase text-peel/20">Bro why aint there any issued items bro are u a underground secret lab</td></tr>';
            return;
        }

        issues.forEach(issue => {
            const item = items.find(i => i.id === issue.item_id);
            const itemName = item ? item.name : `Item #${issue.item_id}`;
            const isReturned = issue.status === 'RETURNED';
            const badgeClass = issue.status === 'OVERDUE'
                ? 'bg-bruise text-white'
                : (isReturned ? 'bg-stem text-white' : 'bg-banana');
            
            const row = document.createElement('tr');
            row.innerHTML = `
                <td class="font-black">${itemName}</td>
                <td class="font-black italic">@${issue.issued_to || issue.assignee}</td>
                <td class="text-center font-black">${issue.quantity}</td>
                <td>
                    <span class="qty-badge ${badgeClass}">
                        ${issue.status}
                    </span>
                </td>
                <td class="font-medium text-sm text-peel/60">${issue.expected_return_date || issue.issue_date || '-'}</td>
                <td class="text-right">
                    ${!isReturned ? `
                        <button class="brutalist-button px-4 py-2 text-xs" onclick="returnIssue(${issue.id})">
                            Return Gear
                        </button>
                    ` : '<span class="font-black text-stem uppercase text-xs">Returned ✓</span>'}
                </td>
            `;
            tbody.appendChild(row);
        });
    } catch (error) {
        console.error('Error loading issues:', error);
    }
}

async function handleCreateIssue(e) {
    e.preventDefault();
    
    const item_id = document.getElementById('issue-item').value;
    const assignee = document.getElementById('issue-assignee').value;
    const issue_date = document.getElementById('issue-date').value;
    const quantity = parseInt(document.getElementById('issue-quantity').value);
    const purposeInput = document.getElementById('issue-purpose');
    const purpose = purposeInput ? purposeInput.value : '';
    const return_date = document.getElementById('issue-return-date').value;
    
    try {
        const response = await fetch(`${API_BASE}/issues`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            },
            body: `item_id=${item_id}&assignee=${encodeURIComponent(assignee)}&issue_date=${encodeURIComponent(issue_date)}&quantity=${quantity}&purpose=${encodeURIComponent(purpose)}&return_date=${return_date}`
        });
        
        const data = await response.json();
        
        if (data.status === 'success') {
            closeDialog('issue-dialog');
            await loadIssues();
            await loadItems();
            loadDashboard();
            showMessage('Issue created successfully!');
        } else {
            showMessage('Error: ' + (data.error || 'Failed to create issue'), 'error');
        }
    } catch (error) {
        showMessage('Error: ' + error.message, 'error');
    }
}

async function returnIssue(issueId) {
    const issues = JSON.parse(localStorage.getItem('issues_cache') || '[]');
    const issue = issues.find(i => i.id === issueId);
    
    if (!issue) {
        showMessage('Issue not found', 'error');
        return;
    }
    
    if (issue.status === 'RETURNED') {
        showMessage('This item has already been returned', 'error');
        return;
    }
    
    const quantity = prompt(`How many units are being returned? (Max: ${issue.quantity - (issue.quantity_returned || 0)})`, issue.quantity - (issue.quantity_returned || 0));
    
    if (quantity === null) return; // Cancelled
    
    const qtyInt = parseInt(quantity);
    if (isNaN(qtyInt) || qtyInt <= 0) {
        showMessage('Please enter a valid quantity', 'error');
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/issues/${issueId}/return`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            },
            body: `quantity_returned=${qtyInt}&condition=Good&notes=Standard Return`
        });
        
        const data = await response.json();
        
        if (data.status === 'success') {
            showMessage('✓ Items returned successfully!');
            await loadIssues();
            await loadItems();
            loadDashboard();
        } else {
            showMessage('Error: ' + (data.error || 'Failed to return items'), 'error');
        }
    } catch (error) {
        showMessage('Connection error: ' + error.message, 'error');
    }
}

async function loadReports() {
    try {
        const response = await fetch(`${API_BASE}/items`, {
            headers: {
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            }
        });
        
        let items = await response.json();
        if (!Array.isArray(items)) items = [];
        
        // Prepare data for charts
        const inventorySummary = {
            labels: [],
            quantities: [],
            values: []
        };
        
        items.forEach(item => {
            inventorySummary.labels.push(item.name);
            inventorySummary.quantities.push(item.current_stock || 0);
            inventorySummary.values.push((item.current_stock || 0) * (item.unit_price || 0));
        });
        
        // If no items, create dummy data
        if (items.length === 0) {
            inventorySummary.labels = ['Sample Item 1', 'Sample Item 2', 'Sample Item 3', 'Sample Item 4'];
            inventorySummary.quantities = [45, 62, 38, 52];
            inventorySummary.values = [450, 620, 380, 520];
        }
        
        renderInventoryChart(inventorySummary);
        renderStockChart(inventorySummary);
    } catch (error) {
        console.error('Error loading reports:', error);
    }
}

async function generateAiReport() {
    const output = document.getElementById('ai-report-output');
    const button = document.getElementById('ai-report-btn');
    if (!output || !button) return;

    output.textContent = 'Generating report...';
    button.disabled = true;
    button.textContent = 'Generating...';

    try {
        const response = await fetch(`${API_BASE}/reports/ai`, {
            method: 'POST',
            headers: {
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            }
        });

        const data = await response.json();

        if (data.status === 'success') {
            output.textContent = data.report;
        } else {
            output.textContent = data.error || 'Failed to generate AI report.';
            showMessage('Error: ' + (data.error || 'Failed to generate AI report'), 'error');
        }
    } catch (error) {
        output.textContent = 'AI report request failed.';
        showMessage('Connection error: ' + error.message, 'error');
    } finally {
        button.disabled = false;
        button.textContent = 'Generate AI Report';
    }
}

function toggleAiChat() {
    const body = document.getElementById('ai-chat-body');
    if (body) body.classList.toggle('hidden');
}

function appendChatMessage(message, role) {
    const messages = document.getElementById('ai-chat-messages');
    const bubble = document.createElement('div');
    bubble.className = role === 'user'
        ? 'border-2 border-peel rounded-banana p-3 bg-pulp font-bold text-sm ml-8'
        : 'border-2 border-peel rounded-banana p-3 bg-banana font-bold text-sm mr-8';
    bubble.textContent = message;
    messages.appendChild(bubble);
    messages.scrollTop = messages.scrollHeight;
}

async function handleAiChat(e) {
    e.preventDefault();

    const input = document.getElementById('ai-chat-input');
    const message = input.value.trim();
    if (!message) return;

    appendChatMessage(message, 'user');
    input.value = '';

    try {
        const response = await fetch(`${API_BASE}/ai/chat`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            },
            body: `message=${encodeURIComponent(message)}`
        });

        const data = await response.json();
        const reply = data.reply || 'No reply available.';
        appendChatMessage(reply, 'assistant');

        // Update model badge
        const modelBadge = document.getElementById('ai-chat-model-badge');
        if (modelBadge && data.model) {
            const shortModel = data.model.split('/').pop() || data.model;
            modelBadge.textContent = shortModel;
        }
    } catch (error) {
        appendChatMessage(`Connection error: ${error.message}`, 'assistant');
    }
}

function renderInventoryChart(data) {
    const ctx = document.getElementById('inventory-chart');
    if (!ctx) return;
    
    // Destroy existing chart if it exists
    const canvasParent = ctx.parentElement;
    if (window.inventoryChartInstance) {
        window.inventoryChartInstance.destroy();
    }
    
    window.inventoryChartInstance = new Chart(ctx, {
        type: 'doughnut',
        data: {
            labels: data.labels,
            datasets: [{
                data: data.quantities,
                backgroundColor: [
                    '#2563eb',
                    '#1e40af',
                    '#3b82f6',
                    '#60a5fa'
                ],
                borderColor: '#fff',
                borderWidth: 2
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            plugins: {
                legend: {
                    position: 'bottom'
                }
            }
        }
    });
}

function renderStockChart(data) {
    const ctx = document.getElementById('stock-chart');
    if (!ctx) return;
    
    // Destroy existing chart if it exists
    if (window.stockChartInstance) {
        window.stockChartInstance.destroy();
    }
    
    window.stockChartInstance = new Chart(ctx, {
        type: 'bar',
        data: {
            labels: data.labels,
            datasets: [{
                label: 'Stock Quantity',
                data: data.quantities,
                backgroundColor: '#16a34a',
                borderColor: '#15803d',
                borderWidth: 1
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            plugins: {
                legend: {
                    display: true
                }
            },
            scales: {
                y: {
                    beginAtZero: true,
                    ticks: {
                        stepSize: 10
                    }
                }
            }
        }
    });
}

// ============================================================================
// UTILITIES
// ============================================================================

function showMessage(message, type = 'success') {
    // Create temporary message
    const msg = document.createElement('div');
    msg.className = type === 'success' ? 'success-message' : 'error-message show';
    msg.textContent = message;
    msg.style.position = 'fixed';
    msg.style.top = '20px';
    msg.style.right = '20px';
    msg.style.zIndex = '2000';
    msg.style.minWidth = '300px';
    
    document.body.appendChild(msg);
    
    setTimeout(() => {
        msg.remove();
    }, 4000);
}

// Status badge styling
const style = document.createElement('style');
style.textContent = `
    .status-badge {
        padding: 4px 8px;
        border-radius: 4px;
        font-size: 12px;
        font-weight: 600;
    }
    
    .status-badge.ISSUED { background-color: #dbeafe; color: #1e40af; }
    .status-badge.PARTIAL { background-color: #fed7aa; color: #92400e; }
    .status-badge.RETURNED { background-color: #dcfce7; color: #166534; }
    
    .badge {
        padding: 4px 8px;
        border-radius: 4px;
        font-size: 12px;
        font-weight: 600;
        background-color: #ede9fe;
        color: #5b21b6;
    }
`;
document.head.appendChild(style);
// ============================================================================
// BACKUP & RESTORE
// ============================================================================

async function createBackup() {
    try {
        const response = await fetch(`${API_BASE}/backup/create`, {
            method: 'POST',
            headers: {
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            }
        });
        
        const data = await response.json();
        
        if (data.status === 'success') {
            addBackupHistory('Created', data.filename);
            
            // Trigger download
            setTimeout(() => {
                window.location.href = `${API_BASE}/backup/download/${data.filename}`;
            }, 500);
            
            showMessage('✓ Backup created and downloaded successfully!');
        } else {
            showMessage('Error: ' + (data.error || 'Failed to create backup'), 'error');
        }
    } catch (error) {
        showMessage('Connection error: ' + error.message, 'error');
    }
}

function addBackupHistory(action, filename) {
    const table = document.getElementById('backup-history');
    if (table.rows.length === 1 && table.rows[0].cells.length === 1) {
        table.innerHTML = '';
    }
    
    const row = table.insertRow(0);
    const dateCell = row.insertCell(0);
    const actionCell = row.insertCell(1);
    
    const now = new Date();
    dateCell.textContent = now.toLocaleString();
    actionCell.textContent = action + (filename ? ` - ${filename}` : '');
}

document.getElementById('backup-file').addEventListener('change', function(e) {
    if (this.files && this.files[0]) {
        const filename = this.files[0].name;
        const statusEl = document.getElementById('backup-status');
        statusEl.textContent = `Selected: ${filename}`;
        statusEl.style.color = '#16a34a';
        document.getElementById('restore-btn').classList.remove('hidden');
    }
});

async function restoreBackup() {
    const fileInput = document.getElementById('backup-file');
    if (!fileInput.files || !fileInput.files[0]) {
        showMessage('No file selected', 'error');
        return;
    }
    
    const confirmed = confirm('Are you sure? This will overwrite your current data with the backup contents.');
    if (!confirmed) return;
    
    const file = fileInput.files[0];
    
    try {
        const fileBuffer = await file.arrayBuffer();
        const response = await fetch(`${API_BASE}/backup/restore`, {
            method: 'POST',
            headers: {
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`,
                'Content-Type': 'application/octet-stream'
            },
            body: fileBuffer
        });
        
        const data = await response.json();
        
        if (data.status === 'success') {
            addBackupHistory('Restored', fileInput.files[0].name);
            showMessage('✓ Backup restored successfully! Reloading...');
            setTimeout(() => location.reload(), 2000);
        } else {
            showMessage('Error: ' + (data.error || 'Failed to restore backup'), 'error');
        }
    } catch (error) {
        showMessage('Connection error: ' + error.message, 'error');
    }
}
