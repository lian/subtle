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
    @interval = "/tmp/watch"
  end

  def run
    begin
      File.open(@interval, "r") do |f|
        @data = f.read
      end
    rescue => err
      p err
    end
  end
end
