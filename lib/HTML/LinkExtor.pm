package HTML::LinkExtor;

=head1 NAME

HTML::LinkExtor - Extract links from an HTML document

=head1 SYNOPSIS

 require HTML::LinkExtor;
 $p = HTML::LinkExtor->new(\&cb, "http://www.sn.no/");
 sub cb {
     my($tag, %links) = @_;
     print "$tag @{[%links]}\n";
 }
 $p->parse_file("index.html");

=head1 DESCRIPTION

I<HTML::LinkExtor> is an HTML parser that extracts links from an
HTML document.  The I<HTML::LinkExtor> is a subclass of
I<HTML::Parser>. This means that the document should be given to the
parser by calling the $p->parse() or $p->parse_file() methods.

=cut

require HTML::Parser;
@ISA = qw(HTML::Parser);
$VERSION = sprintf("%d.%02d", q$Revision: 1.28 $ =~ /(\d+)\.(\d+)/);

use strict;
use vars qw(%LINK_ELEMENT);

# Tags that might contain links and the link attribute name(s)
%LINK_ELEMENT =
(
 a       => 'href',
 applet  => [qw(archive codebase code)],
 area    => 'href',
 base    => 'href',
 bgsound => 'src',
 blockquote => 'cite',
 body    => 'background',
 del     => 'cite',
 embed   => [qw(pluginspage src)],
 form    => 'action',
 frame   => [qw(src longdesc)],
 iframe  => [qw(src longdesc)],
 ilayer  => 'background',
 img     => [qw(src lowsrc longdesc usemap)],
 input   => [qw(src usemap)],
 ins     => 'cite',
 isindex => 'action',
 head    => 'profile',
 layer   => [qw(background src)],
'link'   => 'href',
 object  => [qw(classid codebase data archive usemap)],
'q'      => 'cite',
 script  => [qw(src for)],
 table   => 'background',
 td      => 'background',
 th      => 'background',
 xmp     => 'href',
);

=over 4

=item $p = HTML::LinkExtor->new([$callback[, $base]])

The constructor takes two optional arguments. The first is a reference
to a callback routine. It will be called as links are found. If a
callback is not provided, then links are just accumulated internally
and can be retrieved by calling the $p->links() method.

The $base argument is an optional base URL used to absolutize all URLs found.
You need to have the I<URI> module installed if you provide $base.

The callback is called with the lowercase tag name as first argument,
and then all link attributes as separate key/value pairs.  All
non-link attributes are removed.

=cut

sub new
{
    my($class, $cb, $base) = @_;
    my $self = $class->SUPER::new(start_h => ["_start_tag",
					      "self,tagname,attr"]);
    $self->{extractlink_cb} = $cb;
    if ($base) {
	require URI;
	$self->{extractlink_base} = URI->new($base);
    }
    $self;
}

sub _start_tag
{
    my($self, $tag, $attr) = @_;
    return unless exists $LINK_ELEMENT{$tag};

    my $base = $self->{extractlink_base};
    my $links = $LINK_ELEMENT{$tag};
    $links = [$links] unless ref $links;

    my @links;
    my $a;
    for $a (@$links) {
	next unless exists $attr->{$a};
	push(@links, $a, $base ? URI->new($attr->{$a}, $base)->abs($base)
                               : $attr->{$a});
    }
    return unless @links;
    $self->_found_link($tag, @links);
}

sub _found_link
{
    my $self = shift;
    my $cb = $self->{extractlink_cb};
    if ($cb) {
	&$cb(@_);
    } else {
	push(@{$self->{'links'}}, [@_]);
    }
}

=item $p->links

Returns a list of all links found in the document.  The returned
values will be anonymous arrays with the follwing elements:

  [$tag, $attr => $url1, $attr2 => $url2,...]

The $p->links method will also truncate the internal link list.  This
means that if the method is called twice without any parsing
between them the second call will return an empty list.

Also note that $p->links will always be empty if a callback routine
was provided when the I<HTML::LinkExtor> was created.

=cut

sub links
{
    my $self = shift;
    exists($self->{'links'}) ? @{delete $self->{'links'}} : ();
}

# We override the parse_file() method so that we can clear the links
# before we start a new file.
sub parse_file
{
    my $self = shift;
    delete $self->{'links'};
    $self->SUPER::parse_file(@_);
}

=back

=head1 EXAMPLE

This is an example showing how you can extract links from a document
received using LWP:

  use LWP::UserAgent;
  use HTML::LinkExtor;
  use URI::URL;

  $url = "http://www.sn.no/";  # for instance
  $ua = new LWP::UserAgent;

  # Set up a callback that collect image links
  my @imgs = ();
  sub callback {
     my($tag, %attr) = @_;
     return if $tag ne 'img';  # we only look closer at <img ...>
     push(@imgs, values %attr);
  }

  # Make the parser.  Unfortunately, we don't know the base yet
  # (it might be diffent from $url)
  $p = HTML::LinkExtor->new(\&callback);

  # Request document and parse it as it arrives
  $res = $ua->request(HTTP::Request->new(GET => $url),
                      sub {$p->parse($_[0])});

  # Expand all image URLs to absolute ones
  my $base = $res->base;
  @imgs = map { $_ = url($_, $base)->abs; } @imgs;

  # Print them out
  print join("\n", @imgs), "\n";

=head1 SEE ALSO

L<HTML::Parser>, L<LWP>, L<URI::URL>

=head1 COPYRIGHT

Copyright 1996-1999 Gisle Aas.

This library is free software; you can redistribute it and/or
modify it under the same terms as Perl itself.

=cut

1;
