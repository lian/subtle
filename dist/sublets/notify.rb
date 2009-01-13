#
# Notify
#
# Author: Christoph Kappel
# Contact: unexist@dorfelite.net
# Description: Notify example
# Version: 0.1
# Date: Sat Sep 13 19:00 CET 2008
# $Id$
#

class Notify < Sublet
  def initialize
    self.interval = "/tmp/watch"
  end

  def run
    file = ""

    # We never begin/rescue here to unload the
    # sublet if the watch file doesn't exist
    File.open(self.interval, "r") do |f|
      file = f.read
    end

    self.data = file || "subtle"
  end
end
