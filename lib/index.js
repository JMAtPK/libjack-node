const path = require('path')
const bindings = require(path.join(__dirname, '..', 'build', 'Release', 'libjack_node.node'))
module.exports = { JackClient: bindings.JackClient }
