// Main application script
function init() {
    console.log("App initialized");
    const items = document.querySelectorAll('.item');
    
    // Process each item
    items.forEach(function(item) {
        item.addEventListener('click', function() {
            /* Handle click event */
            console.log('Clicked: ' + item.id);
        });
    });
}

/*
 * Helper function for data processing
 */
function processData(data) {
    // Filter valid entries
    const valid = data.filter(function(entry) {
        return entry.active && entry.name !== '';
    });
    
    return valid.map(function(entry) {
        return {
            id: entry.id,
            label: entry.name
        };
    });
}

// String with special chars
const msg = "Hello /* not a comment */ world";
const msg2 = '// also not a comment';
const tmpl = `template with // and /* inside`;

document.addEventListener('DOMContentLoaded', init);
