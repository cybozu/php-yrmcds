Coding tips
===========

C programming language standard
-------------------------------

Follow [C99][] standard just as [libyrmcds][] do.

Git subtree
-----------

This repository contains two subtrees; one is for [libyrmcds][], and
another is for [LZ4][].

Subtrees were created as follows:

```
git remote add -f libyrmcds https://github.com/cybozu/libyrmcds
git remote add -f lz4 https://github.com/Cyan4973/lz4
git subtree add -P libyrmcds libyrmcds master --squash
git subtree add -P libyrmcds/lz4 lz4 r127 --squash
```

Your cloned repository should have these two remotes too.
To update libyrmcds, do:

```
git fetch libyrmcds
git subtree pull -P libyrmcds libyrmcds master --squash
```

PHPDoc
------

`make -f Makefile.phpdoc setup` will instruct you how to setup
PHP executable for PHPDoc on your local machine.  Follow it, then
run `make -f Makefile.phpdoc phpdoc` to install PHPDoc.

To generate HTML documents, run `make -f Makefile.phpdoc`.


[C99]: https://en.wikipedia.org/wiki/C99
[libyrmcds]: https://github.com/cybozu/libyrmcds
[LZ4]: https://github.com/Cyan4973/lz4
