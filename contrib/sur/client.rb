#
# @package sur
#
# @file Client functions
# @author Christoph Kappel <unexist@dorfelite.net>
# @version $Id$
#
# This program can be distributed under the terms of the GNU GPL.
# See the file COPYING.
#

require "rubygems"
require "fileutils"
require "tempfile"
require "yaml"
require "curb"
require "archive/tar/minitar"

require "subtle/sur/specification"

# Fixme: Different class paths
module Sur
  class Specification < Subtle::Sur::Specification
  end
end

module Subtle # {{{
  module Sur # {{{
    # Client class for interaction with the user
    class Client # {{{
      # Remote repository host
      HOST = "http://sur.subtle.de"

      # Local sublet cache
      attr_accessor :cache_local

      # Remote sublet cache
      attr_accessor :cache_remote

      # Path to local cache file
      attr_accessor :path_local

      # Path to remote cache file
      attr_accessor :path_remote

      # Path to icons
      attr_accessor :path_icons

      # Path of specification
      attr_accessor :path_specs

      # Path to sublets
      attr_accessor :path_sublets

      ## Sur::Client::initialize {{{
      # Create a new Client object
      #
      # @return [Object] New Client
      #
      # @since 0.0
      #
      # @example
      #   client = Sur::Client.new
      #   => #<Sur::Client:xxx>

      def initialize
        # Paths
        xdg_cache_home = File.join((ENV["XDG_CACHE_HOME"] ||
          File.join(ENV["HOME"], ".cache")), "sur")
        xdg_data_home  = File.join((ENV["XDG_DATA_HOME"] ||
          File.join(ENV["HOME"], ".local", "share")), "subtle")

        @path_local   = File.join(xdg_cache_home, "local.yaml")
        @path_remote  = File.join(xdg_cache_home, "remote.yaml")
        @path_icons   = File.join(xdg_data_home,  "icons")
        @path_specs   = File.join(xdg_data_home,  "specifications")
        @path_sublets = File.join(xdg_data_home,  "sublets")

        # Create folders
        [ xdg_cache_home, xdg_data_home, @path_icons, @path_specs,
            @path_sublets ].each do |p|
          FileUtils.mkdir_p([ p ]) unless(File.exist?(p))
        end

        # Load local cache
        if(File.exist?(@path_local))
          yaml = YAML::load(File.open(@path_local))

          @cache_local = YAML::load(yaml)
        else
          build_local
        end

        # Load remote cache
        if(File.exist?(@path_remote))
          # Check age of cache
          if(86400 > (Time.now - File.new(@path_remote).ctime))
            yaml = YAML::load(File.open(@path_remote))

            @cache_remote = YAML::load(yaml)
          else
            build_remote
          end
        end

      end # }}}

      ## Sur::Client::annotate {{{
      # Mark a sublet as to be reviewed
      #
      # @param [String, #read]  name     Name of the Sublet
      # @param [String, #read]  version  Version of the Sublet
      #
      # @raise [String] Annotate error
      # @since 0.2
      #
      # @example
      #   Sur::Client.new.submit("sublet")
      #   => nil

      def annotate(name, version = nil)
        # Check if sublet exists
        if((specs = search(name, @cache_remote, version, false)) && specs.empty?)
          raise "Sublet `#{name}' does not exist"
        end

        spec = specs.first
        c    = Curl::Easy.new(HOST + "/annotate")

        c.http_post(
          Curl::PostField.content("digest", spec.digest),
          Curl::PostField.content("user",   ENV["USER"])
        )

        # Check server response
        case c.response_code
          when 200 then return
          when 404 then raise "Couldn't find sublet"
          when 415 then raise "Couldn't store annotation"
          else raise "Couldn't annotate sublet"
        end
      end # }}}

      ## Sur::Client::build {{{
      # Build a sublet package
      #
      # @param [String, #read]  file  Spec file name
      #
      # @see Sur::Specification.load_file
      # @raise [String] Build error
      # @since 0.1
      #
      # @example
      #   Sur::Client.new.build("sublet.spec")
      #   => nil

      def build(file)
        spec = Sur::Specification.load_spec(file)

        # Check specification
        if(spec.valid?)
          begin
            sublet = "%s-%s.sublet" % [
              File.join(Dir.pwd, spec.name.downcase),
              spec.version
            ]
            opts = { :mode => 644, :mtime => Time.now.to_i }

            # Check if files exist
            (spec.files | spec.icons).each do |f|
              unless(File.exist?(File.join(File.dirname(file), f)))
                puts ">>> ERROR: Couldn't find file `#{f}'"
                raise
              end
            end

            # Create tar file
            File.open(sublet, "wb") do |output|
              Archive::Tar::Minitar::Writer.open(output) do |tar|
                # Add Spec
                tar.add_file(File.basename(file), opts) do |os|
                  os.write(IO.read(file))
                end

                # Add files
                spec.files.each do |f|
                  tar.add_file(File.basename(f), opts) do |os|
                    os.write(IO.read(File.join(File.dirname(file), f)))
                  end
                end

                # Add icons
                spec.icons.each do |f|
                  tar.add_file(File.basename(f), opts) do |os|
                    os.write(IO.read(File.join(File.dirname(file), f)))
                  end
                end
              end
            end

            puts ">>> Created sublet `#{File.basename(sublet)}'"
          rescue
            raise "Couldn't create tgz file"
          end
        else
          spec.validate
        end
      end # }}}

      ## Sur::Client::install {{{
      # Install a new Sublet
      #
      # @param [String, #read]  name      Name of the Sublet
      # @param [String, #read]  version   Version of the Sublet
      # @param [String, #read]  use_tags  Use tags
      # @param [Bool,   #read]  reload    Reload sublets after installing
      #
      # @raise [String] Install error
      # @since 0.0
      #
      # @example
      #   Sur::Client.new.install("sublet")
      #   => nil

      def install(name, version = nil, use_tags = false, reload = false)
        build_local  if(@cache_local.nil?)
        build_remote if(@cache_remote.nil?)

        # Check if sublet is already installed
        if((specs = search(name, @cache_local, version, use_tags)) && !specs.empty?)
          raise "Sublet `#{name}' is already installed"
        end

        # Check if sublet is local
        if(File.exist?(name))
          install_sublet(name)

          reload_sublets if(reload)

          return
        end

        # Check if remote sublet exists
        if((specs = search(name, @cache_remote, version, use_tags)) && specs.empty?)
          raise "Couldn't find sublet `#{name}' in remote repository"
        end

        # Check dependencies
        spec = specs.first
        raise "Couldn't install sublet `#{name}'" unless(spec.satisfied?)

        # Create tempfile
        temp = Tempfile.new(spec.to_s)

        # Fetch file
        c = Curl::Easy.new(HOST + "/get/" + spec.digest)

        # Progress handler
        c.on_progress do |dl_total, dl_now, ul_total, ul_now|
          progress(">>> Fetching sublet `#{spec.to_s}'", dl_total, dl_now)
          true
        end

        c.perform
        puts

        # Check server response
        case c.response_code
          when 200
            body = c.body_str

            # Write file to tempfile
            File.open(temp.path, "w+") do |out|
              out.write(body)
            end

            # Install sublet
            install_sublet(temp.path)

            reload_sublets if(reload)
          else
            raise "Coudln't retrieve sublet. Server returned HTTP error #{c.response_code}"
        end
      end # }}}

      ## Sur::Client::list {{{
      # List the Sublet in a repository
      #
      # @param [String, #read]  repo  Repo name
      # @param [Bool,   #read]  color  Use colors
      #
      # @since 0.0
      #
      # @example
      #   Sur::Client.new.list("remote")
      #   => nil

      def list(repo, color)
        # Select cache
        case repo
          when "local" then specs = @cache_local
          when "remote"
            build_remote if(@cache_remote.nil?)

            specs = @cache_remote
        end

        dump(specs, color)
      end # }}}

      ## Sur::Client::notes {{{
      # Show notes about an installed sublet if any
      #
      # @param [String, #read]  name  Name of the Sublet
      #
      # @raise [String] Notes error
      # @since 0.2
      #
      # @example
      #   Sur::Client.new.notes("sublet")
      #   => "Notes"

      def notes(name)
        build_local if(@cache_local.nil?)

        # Check if sublet is already installed
        if((specs = search(name, @cache_local)) && !specs.empty?)
          spec = specs.first

          show_notes(spec)
        end
      end # }}}

      ## Sur::Client::query {{{
      # Query repo for a Sublet
      #
      # @param [String, #read]  query      Query string
      # @param [String, #read]  repo       Repo name
      # @param [Bool,   #read]  use_regex  Use regex
      # @param [Bool,   #read]  use_tags   Use tags
      # @param [Bool,   #read]  use_color  Use colors
      #
      # @raise [String] Query error
      # @since 0.0
      #
      # @example
      #   Sur::Client.new.query("sublet", "remote")
      #   => nil

      def query(query, repo, version = nil, use_regex = false, use_tags = false, use_color = true)
        case repo
          when "local"
            unless((specs = search(query, @cache_local, version,
                use_regex, use_tags)) && !specs.empty?)
              raise "Couldn't find `#{query}' in local repository"
            end
          when "remote"
            build_remote if(@cache_remote.nil?)
            unless((specs = search(query, @cache_remote, version,
                use_regex, use_tags)) && !specs.empty?)
              raise "Couldn't find `#{query}' in remote repository"
            end
        end

        dump(specs, use_color)
      end # }}}

      ## Sur::Client::reorder {{{
      # Reorder install Sublet
      #
      # @example
      #   Sur::Client.new.reorder
      #   => nil

      def reorder
        build_local if(@cache_local.nil?)

        i     = 0
        list  = []
        files = Dir[@path_sublets + "/*"]

        # Show menu
        @cache_local.each do |s|
          puts "(%d) %s (%s)" % [ i + 1, s.name, s.version ]

          # Check for match if sublet was reordered
          files.each do |f|
            s.files.each do |sf|
              a = File.basename(f)
              b = File.basename(sf)

              if(a == b || File.fnmatch("[0-9_]*#{b}", a))
                list.push([ s.name.downcase, a])
              end
            end
          end

          i += 1
        end

        # Get new order
        puts "\n>>> Enter new numbers separated by blanks:\n"
        printf ">>> "

        line = STDIN.readline
        i    = 0

        if("\n" != line)
          line.split(" ").each do |tok|
            idx = tok.to_i

            name, file = list.at(idx -1)

            i        += 10
            new_path  = '%s/%d_%s.rb' % [ @path_sublets, i, name ]

            # Check if file exists before moving
            unless(File.exist?(new_path))
              FileUtils.mv(File.join(@path_sublets, file), new_path)
            end
          end

          build_local #< Update local cache
        end
      end # }}}

      ## Sur::Client::submit {{{
      # Submit a Sublet to a repository
      #
      # @param [String, #read]  file  Sublet package
      #
      # @raise [String] Submit error
      # @since 0.0
      #
      # @example
      #   Sur::Client.new.submit("sublet")
      #   => nil

      def submit(file)
        if(!file.nil? && File.file?(file) && ".sublet" == File.extname(file))
          spec = Sur::Specification.extract_spec(file)

          if(spec.valid?)
            c      = Curl::Easy.new(HOST + "/submit")
            @file  = File.basename(file)

            c.multipart_form_post = true

            # Progress handler
            c.on_progress do |dl_total, dl_now, ul_total, ul_now|
              progress(">>> Submitting sublet `#{@file}'", ul_total, ul_now)
              true
            end

            c.http_post(
              Curl::PostField.file("file", file),
              Curl::PostField.content("user", ENV["USER"])
            )
            puts ""

            # Check server response
            case c.response_code
              when 200 then build_remote
              when 406 then raise "Couldn't overwrite sublet"
              when 409 then raise "Couldn't overwrite sublet version"
              when 415 then raise "Couldn't store sublet"
              else raise "Couldn't submit sublet"
            end
          else
            spec.validate()
          end
        else
          raise "Couldn't find file `#{file}'"
        end
      end # }}}

      ## Sur::Client::uninstall {{{
      # Uninstall a Sublet
      #
      # @param [String, #read]  name      Name of the Sublet
      # @param [String, #read]  version   Version of the Sublet
      # @param [Bool,   #read]  use_tags  Use tags
      # @param [Bool,   #read]  reload    Reload sublets after uninstalling
      #
      # @raise [String] Uninstall error
      # @since 0.0
      #
      # @example
      #   Sur::Client.new.uninstall("sublet")
      #   => nil

      def uninstall(name, version = nil, use_tags = false, reload = false)
        build_local if(@cache_local.nil?)

        # Check if sublet is installed
        if((specs = search(name, @cache_local, version, use_tags)) && !specs.empty?)
          spec = specs.first

          # Uninstall files
          spec.files.each do |f|
            # Check for match if sublet was reordered
            Dir[@path_sublets + "/*"].each do |file|
              a = File.basename(f)
              b = File.basename(file)

              if(a == b || File.fnmatch("[0-9_]*#{a}", b))
                puts ">>>>>> Uninstallling file `#{b}'"
                FileUtils.remove_file(File.join(@path_sublets, b))
              end
            end
          end

          # Uninstall icons
          spec.icons.each do |f|
            puts ">>>>>> Uninstallling icon `#{f}'"
            FileUtils.remove_file(File.join(@path_icons, File.basename(f)))
          end

          # Uninstall specification
          puts ">>>>>> Uninstallling specification `#{spec.to_s}.spec'"
          FileUtils.remove_file(File.join(@path_specs, spec.to_s + ".spec"))

          puts ">>> Uninstalled sublet #{spec.to_s}"

          build_local

          reload_sublets if(reload)
        else
          raise "Couldn't find sublet `#{name}' in local repository"
        end
      end # }}}

      ## Sur::Client::update {{{
      # Update a repository
      #
      # @param [String, #read]  repo  Repo name
      #
      # @since 0.0
      #
      # @example
      #   Sur::Client.new.update("remote")
      #   => nil

      def update(repo)
        case repo
          when "local"
            build_local
          when "remote"
            build_remote
        end
      end # }}}

      ## Sur::Client::upgrade {{{
      # Upgrade all Sublets
      #
      # @param [Bool, #read]  color   Use colors
      # @param [Bool, #read]  reload  Reload sublets after uninstalling
      # @param [Bool, #read]  assume  Whether to assume yes
      #
      # @raise Upgrade error
      # @since 0.2
      #
      # @example
      #   Sur::Client.new.upgrade
      #   => nil

      def upgrade(use_color = true, reload = false, assume = false)
        build_remote

        list = []

        # Iterate over server-side sorted list
        @cache_local.each do |spec_a|
          @cache_remote.each do |spec_b|
            if(spec_a.name == spec_b.name and
                spec_a.version.to_f < spec_b.version.to_f)
              list << spec_b

              if(use_color)
                puts ">>> %s: %s -> %s" % [
                  spec_a.name,
                  colorize(5, spec_a.version),
                  colorize(2, spec_b.version)
                ]
              else
                 puts ">>> %s: %s -> %s" % [
                  spec_a.name, spec_a.version, spec_b.version
                ]
              end

              break
            end
          end
        end

        return if(list.empty?)

        # Really?
        unless(assume)
          print ">>> Upgrade sublets (y/n)? "

          return unless("y" == STDIN.readline.strip.downcase)
        end

        # Finally upgrade
        list.each do |spec|
          uninstall(spec.name)
          install(spec.name)
        end

        reload_sublets if(reload)
      end # }}}

      private

      def progress(mesg, total, now) # {{{
        if(0.0 < total)
          percent = (now.to_f * 100 / total.to_f).floor

          $stdout.sync = true
          print "\r#{mesg} (#{percent}%)"
        end
      end # }}}

      def search(query, repo, version = nil, use_regex = false, use_tags = false) # {{{
        results = []

        # Search in repo
        repo.each do |s|
          if(!query.nil? && (s.name.downcase == query.downcase ||
              (use_regex && s.name.downcase.match(query)) ||
              (use_tags  && s.tags.include?(query.capitalize))))
            # Check version?
            if(version.nil? || s.version == version)
              results.push(s)
            end
          end
        end

        results
      end # }}}

      def build_local # {{{
        @cache_local = []

        # Check installed sublets
        Dir[@path_specs + "/*"].each do |file|
          begin
            spec = Sur::Specification.load_spec(file)

            # Validate
            if(spec.valid?)
              spec.path = file
              @cache_local.push(spec)
            else
              spec.validate()
            end
          rescue
            puts ">>> WARNING: Couldn't parse specification `#{file}'"
          end
        end

        puts ">>> Updated local cache with #{@cache_local.size} entries"

        @cache_local.sort { |a, b| [ a.name, a.version ] <=> [ b.name, b.version ] }

        # Dump yaml file
        yaml = @cache_local.to_yaml
        File.open(@path_local, "w+") do |out|
          YAML::dump(yaml, out)
        end
      end # }}}

      def build_remote # {{{
        @cache_remote = []

        # Fetch list
        c = Curl::Easy.perform(HOST + "/list")

        if(200 == c.response_code)
          @cache_remote = YAML::load(YAML::load(c.body_str))

          puts ">>> Updated remote cache with #{@cache_remote.size} entries"

          @cache_remote.sort do |a, b|
            [ a.name, a.version ] <=> [ b.name, b.version ]
          end

          # Dump yaml file
          yaml = @cache_remote.to_yaml
          File.open(@path_remote, "w+") do |out|
            YAML::dump(yaml, out)
          end
        else
          raise "Couldn't get list from remote server"
        end
      end # }}}

      def colorize(color, text, bold = false, mode = :fg) # {{{
        c = true == bold ? 1 : 30 + color  #< Bold mode
        m = :fg == mode  ? 1 :  3 #< Color mode

        "\033[#{m};#{c}m#{text}\033[m"
      end # }}}

      def dump(specs, use_color) # {{{
        i = 1
        specs.sort { |a, b| [ a.name, a.version ] <=> [ b.name, b.version ] }
        specs.each do |s|

          # Find if installed
          installed = ""
          @cache_local.each do |cs|
            if(cs.name == s.name)
              installed = "[%s installed]" % [ cs.version ]
              break
            end
          end

          # Do we like colors?
          if(use_color)
            puts "%s %s %s %s" % [
              colorize(2, i.to_s, false, :bg),
              colorize(1, s.name.downcase, true),
              colorize(2, s.version),
              colorize(2, installed, false, :bg)
            ]
            puts "   %s" % [ s.description ]

            unless(s.tags.empty?)
              puts "   %s" % [ s.tags.map { |t| colorize(5, "##{t}") }.join(" ") ]
            end
          else
            puts "(%d) %s %s %s" % [ i, s.name.downcase, s.version, installed ]
            puts "   %s" % [ s.description ]

            unless(s.tags.empty?)
              puts "   %s" % [ s.tags.map { |t| "##{t}" }.join(" ") ]
            end
          end

          i += 1
        end
      end # }}}

      def show_notes(spec) # {{{
        unless(spec.notes.nil? or spec.notes.empty?)
          puts
          puts spec.notes
          puts
        end
      end # }}}

      def install_sublet(sublet) # {{{
        spec = Sur::Specification.extract_spec(sublet)

        # Validate spec
        if(spec.satisfied?)
          File.open(sublet, "rb") do |input|
            Archive::Tar::Minitar::Input.open(input) do |tar|
              tar.each do |entry|
                case(File.extname(entry.full_name))
                  when ".spec" then
                    puts ">>>>>> Installing specification `#{spec.to_s}.spec'"
                    path = File.join(@path_specs, spec.to_s + ".spec")
                  when ".xbm" then
                    puts ">>>>>> Installing icon `#{entry.full_name}'"
                    path = File.join(@path_icons, entry.full_name)
                  else
                    puts ">>>>>> Installing file `#{entry.full_name}'"
                    path = File.join(@path_sublets, entry.full_name)
                end

                # Install file
                File.open(path, "w+") do |output|
                  output.write(entry.read)
                end
              end
            end

            puts ">>> Installed sublet #{spec.to_str}"

            show_notes(spec)
            build_local
          end
        end
      end # }}}

      def reload_sublets # {{{
        begin
          require "subtle/subtlext"

          Subtlext::Subtle.reload_sublets
        rescue
          raise "Couldn't reload sublets"
        end
      end # }}}
    end # }}}
  end # }}}
end # }}}

# vim:ts=2:bs=2:sw=2:et:fdm=marker
