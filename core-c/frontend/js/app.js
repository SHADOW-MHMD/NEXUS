// NEXUS Inventory Manager - Frontend Application

const API_BASE = 'http://localhost:8080/api';
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
    } catch (error) {
        console.error('Error loading dashboard:', error);
    }
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
        
        const items = await response.json();
        if (!Array.isArray(items)) items = [];
        
        // Cache items
        localStorage.setItem('items_cache', JSON.stringify(items));
        
        // Populate table
        const tbody = document.getElementById('inventory-tbody');
        tbody.innerHTML = '';
        
        items.forEach(item => {
            const row = document.createElement('tr');
            row.innerHTML = `
                <td>${item.sku}</td>
                <td>${item.name}</td>
                <td>${item.quantity}</td>
                <td>${item.location || '-'}</td>
                <td>
                    <button class="btn" onclick="editItem(${item.id})">Edit</button>
                    <button class="btn btn-danger" onclick="deleteItem(${item.id})">Delete</button>
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
    const quantity = parseInt(document.getElementById('item-quantity').value);
    
    try {
        const response = await fetch(`${API_BASE}/items`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            },
            body: `sku=${encodeURIComponent(sku)}&name=${encodeURIComponent(name)}&description=${encodeURIComponent(description)}&quantity=${quantity}`
        });
        
        const data = await response.json();
        
        if (data.status === 'success') {
            closeDialog('item-dialog');
            loadItems();
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
    
    filtered.forEach(item => {
        const row = document.createElement('tr');
        row.innerHTML = `
            <td>${item.sku}</td>
            <td>${item.name}</td>
            <td>${item.quantity}</td>
            <td>${item.location || '-'}</td>
            <td>
                <button class="btn" onclick="editItem(${item.id})">Edit</button>
                <button class="btn btn-danger" onclick="deleteItem(${item.id})">Delete</button>
            </td>
        `;
        tbody.appendChild(row);
    });
}

function editItem(itemId) {
    // TODO: Implement edit item
    console.log('Edit item:', itemId);
}

function deleteItem(itemId) {
    if (confirm('Are you sure you want to delete this item?')) {
        // TODO: Implement delete item
        console.log('Delete item:', itemId);
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
        
        const users = await response.json();
        console.log('Users loaded:', users);
        if (!Array.isArray(users)) users = [];
        
        // Cache users
        localStorage.setItem('users_cache', JSON.stringify(users));
        
        // Populate table
        const tbody = document.getElementById('users-tbody');
        tbody.innerHTML = '';
        
        if (users.length === 0) {
            tbody.innerHTML = '<tr><td colspan="5" style="text-align: center; padding: 20px;">No users found</td></tr>';
            return;
        }
        
        users.forEach(user => {
            const row = document.createElement('tr');
            row.innerHTML = `
                <td>${user.username}</td>
                <td>${user.full_name || '-'}</td>
                <td>${user.email || '-'}</td>
                <td><span class="badge">${user.role}</span></td>
                <td>
                    <button class="btn" onclick="editUser(${user.id})">Edit</button>
                    <button class="btn btn-danger" onclick="deleteUser(${user.id})">Delete</button>
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
    const email = document.getElementById('user-email').value;
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
            loadUsers();
            showMessage('User created successfully!');
        } else {
            showMessage('Error: ' + (data.error || 'Failed to create user'), 'error');
        }
    } catch (error) {
        showMessage('Error: ' + error.message, 'error');
    }
}

function editUser(userId) {
    console.log('Edit user:', userId);
}

function deleteUser(userId) {
    if (confirm('Are you sure you want to delete this user?')) {
        console.log('Delete user:', userId);
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
        
        const issues = await response.json();
        if (!Array.isArray(issues)) issues = [];
        
        // Cache issues
        localStorage.setItem('issues_cache', JSON.stringify(issues));
        
        // Get items for reference
        const items = JSON.parse(localStorage.getItem('items_cache') || '[]');
        
        // Populate table
        const tbody = document.getElementById('issues-tbody');
        tbody.innerHTML = '';
        
        issues.forEach(issue => {
            const item = items.find(i => i.id === issue.item_id);
            const itemName = item ? item.name : `Item #${issue.item_id}`;
            
            const row = document.createElement('tr');
            row.innerHTML = `
                <td>${itemName}</td>
                <td>${issue.assignee}</td>
                <td>${issue.quantity}</td>
                <td><span class="status-badge" style="background-color: ${issue.status === 'RETURNED' ? '#28a745' : '#ffc107'}">${issue.status}</span></td>
                <td>${issue.expected_return_date || '-'}</td>
                <td>
                    ${issue.status !== 'RETURNED' ? `<button class="btn" onclick="returnIssue(${issue.id})">Return</button>` : '<span style="color: #28a745;">✓ Returned</span>'}
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
    const quantity = parseInt(document.getElementById('issue-quantity').value);
    const purpose = document.getElementById('issue-purpose').value;
    const return_date = document.getElementById('issue-return-date').value;
    
    try {
        const response = await fetch(`${API_BASE}/issues`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            },
            body: `item_id=${item_id}&assignee=${encodeURIComponent(assignee)}&quantity=${quantity}&purpose=${encodeURIComponent(purpose)}&return_date=${return_date}`
        });
        
        const data = await response.json();
        
        if (data.status === 'success') {
            closeDialog('issue-dialog');
            loadIssues();
            showMessage('Issue created successfully!');
        } else {
            showMessage('Error: ' + (data.error || 'Failed to create issue'), 'error');
        }
    } catch (error) {
        showMessage('Error: ' + error.message, 'error');
    }
}

function returnIssue(issueId) {
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
    
    const confirmed = confirm(`Return ${issue.quantity} unit(s) of item ${issue.item_id} (currently issued to ${issue.assignee})?`);
    
    if (!confirmed) return;
    
    // Mark as returned by updating status
    const returnedIssues = issues.map(i => {
        if (i.id === issueId) {
            return {...i, status: 'RETURNED', actual_return_date: new Date().toISOString().split('T')[0]};
        }
        return i;
    });
    
    localStorage.setItem('issues_cache', JSON.stringify(returnedIssues));
    
    // Reload to show updated status
    loadIssues();
    showMessage('Item returned successfully!');
}

async function loadReports() {
    try {
        const response = await fetch(`${API_BASE}/items`, {
            headers: {
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            }
        });
        
        const items = await response.json();
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
    if (table.rows[0].cells[0].textContent === 'No backups yet') {
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
        document.getElementById('restore-btn').style.display = 'block';
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
    
    const formData = new FormData();
    formData.append('backup', fileInput.files[0]);
    
    try {
        const response = await fetch(`${API_BASE}/backup/restore`, {
            method: 'POST',
            headers: {
                'Authorization': `Bearer ${localStorage.getItem('auth_token')}`
            },
            body: formData
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
