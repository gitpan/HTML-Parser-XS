use strict;
print "1..2\n";

use HTML::Parser ();
my $p = HTML::Parser->new(xml_mode => 1,
			 );

my $text = "";
$p->handler(start =>
	    sub {
		 my($tag, $attr) = @_;
		 $text .= "S[$tag";
		 for my $k (sort keys %$attr) {
		     my $v =  $attr->{$k};
		     $text .= " $k=$v";
		 }
		 $text .= "]";
	     }, "tagname,attr");
$p->handler(end =>
	     sub {
		 $text .= "E[" . shift() . "]";
	     }, "tagname");
$p->handler(process => 
	     sub {
		 $text .= "PI[" . shift() . "]";
	     }, "token1");
$p->handler(text =>
	     sub {
		 $text .= shift;
	     }, "origtext");

my $xml = <<'EOT';
<?xml version="1.0"?>
<?IS10744:arch name="html"?><!-- comment -->
<DOC>
<title html="h1">My first architectual document</title>
<author html="address">Geir Ove Gronmo, grove@infotek.no</author>
<para>This is the first paragraph in this document</para>
<para html="p">This is the second paragraph</para>
<para/>
</DOC>
EOT

$p->parse($xml)->eof;

print "not " unless $text eq <<'EOT';  print "ok 1\n";
PI[xml version="1.0"]
PI[IS10744:arch name="html"]
S[DOC]
S[title html=h1]My first architectual documentE[title]
S[author html=address]Geir Ove Gronmo, grove@infotek.noE[author]
S[para]This is the first paragraph in this documentE[para]
S[para html=p]This is the second paragraphE[para]
S[para]E[para]
E[DOC]
EOT

$text = "";
$p->xml_mode(0);
$p->parse($xml)->eof;

print "not " unless $text eq <<'EOT';  print "ok 2\n";
PI[xml version="1.0"?]
PI[IS10744:arch name="html"?]
S[doc]
S[title html=h1]My first architectual documentE[title]
S[author html=address]Geir Ove Gronmo, grove@infotek.noE[author]
S[para]This is the first paragraph in this documentE[para]
S[para html=p]This is the second paragraphE[para]
S[para/]
E[doc]
EOT

