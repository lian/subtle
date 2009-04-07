# 
# This program can be distributed under the terms of the GNU GPL.
# See the file COPYING.
# 
# $Id$
# 

require("../subtlext")
require("test/unit")

class Test::Unit::TestCase
  def setup # {{{
    # Test: Connection
    @subtle = Subtle.new(":2")
    assert_not_nil(@subtle)
    assert_instance_of(Subtle, @subtle)
  end # }}]
end

require("unit/sublet_test")
require("unit/client_test")
require("unit/fixnum_test")
require("unit/gravity_test")
require("unit/tag_test")
require("unit/view_test")
