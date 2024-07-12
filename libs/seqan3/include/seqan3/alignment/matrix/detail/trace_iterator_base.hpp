// -----------------------------------------------------------------------------------------------------
// Copyright (c) 2006-2023, Knut Reinert & Freie Universität Berlin
// Copyright (c) 2016-2023, Knut Reinert & MPI für molekulare Genetik
// This file may be used, modified and/or redistributed under the terms of the 3-clause BSD-License
// shipped with this file and also available at: https://github.com/seqan/seqan3/blob/master/LICENSE.md
// -----------------------------------------------------------------------------------------------------

/*!\file
 * \brief Provides seqan3::detail::trace_iterator_base.
 * \author Rene Rahn <rene.rahn AT fu-berlin.de>
 */

#pragma once

#include <concepts>
#include <ranges>

#include <seqan3/alignment/matrix/detail/trace_directions.hpp>
#include <seqan3/alignment/matrix/detail/two_dimensional_matrix_iterator_base.hpp>
#include <seqan3/alignment/matrix/detail/two_dimensional_matrix_iterator_concept.hpp>

namespace seqan3::detail
{

/*!\brief A CRTP-base class for trace iterator implementations for the alignment algorithms.
 * \ingroup alignment_matrix
 * \implements std::forward_iterator
 *
 * \tparam derived_t The derived iterator type.
 * \tparam matrix_iter_t The wrapped matrix iterator; must model seqan3::detail::two_dimensional_matrix_iterator and
 *                       the iterator's value type must be the same as seqan3::detail::trace_directions, i.e.
 *                       `std::same_as<std::iter_value_t<matrix_iter_t>, trace_directions>` must evaluate to `true`.
 *
 * \details
 *
 * This iterator follows the trace path generated by the alignment algorithm. It wraps an underlying
 * seqan3::detail::two_dimensional_matrix_iterator over a trace matrix, whose value type is
 * seqan3::detail::trace_directions. The iterator moves along the trace path until it finds a cell with
 * seqan3::detail::trace_directions::none.
 * Accordingly, when advancing this iterator, it actually moves from right to left and from bottom to top in the
 * underlying matrix. When the iterator is dereferenced, it outputs any of the following direction:
 * seqan3::detail::trace_directions::diagonal, seqan3::detail::trace_directions::up, or
 * seqan3::detail::trace_directions::left.
 *
 * In addition, the iterator provides an additional member to access the current position as a
 * seqan3::detail::matrix_coordinate.
 *
 * This iterator also models the [Cpp17ForwardIterator](https://en.cppreference.com/w/cpp/named_req/ForwardIterator).
 * Note, it does not directly dereference the actual trace direction stored in the underlying matrix.
 * Thus, it cannot be used as an output iterator.
 *
 * ### Overloading the behaviour
 *
 * The behaviour of following a trace direction can be customised through the derived type by overloading the functions
 * * seqan3::detail::trace_iterator_base::go_diagonal,
 * * seqan3::detail::trace_iterator_base::go_left, and
 * * seqan3::detail::trace_iterator_base::go_up.
 *
 * In the default implementation they move along an unbanded matrix. This means, they go to the previous cell in the
 * respective direction.
 */
template <typename derived_t, two_dimensional_matrix_iterator matrix_iter_t>
class trace_iterator_base
{
private:
    static_assert(std::same_as<std::iter_value_t<matrix_iter_t>, trace_directions>,
                  "Value type of the underlying iterator must be seqan3::detail::trace_directions.");

    //!\brief Befriend with corresponding const_iterator.
    template <typename other_derived_t, two_dimensional_matrix_iterator other_matrix_iter_t>
    friend class trace_iterator_base;

    //!\brief Befriend the derived iterator class to allow calling the private constructors.
    friend derived_t;

    /*!\name Constructors, destructor and assignment
     * \{
     */
    constexpr trace_iterator_base() = default;                                        //!< Defaulted.
    constexpr trace_iterator_base(trace_iterator_base const &) = default;             //!< Defaulted.
    constexpr trace_iterator_base(trace_iterator_base &&) = default;                  //!< Defaulted.
    constexpr trace_iterator_base & operator=(trace_iterator_base const &) = default; //!< Defaulted.
    constexpr trace_iterator_base & operator=(trace_iterator_base &&) = default;      //!< Defaulted.
    ~trace_iterator_base() = default;                                                 //!< Defaulted.

    /*!\brief Constructs from the underlying trace matrix iterator indicating the start of the trace path.
     * \param[in] matrix_iter The underlying matrix iterator.
     */
    constexpr trace_iterator_base(matrix_iter_t const matrix_iter) noexcept : matrix_iter{matrix_iter}
    {
        set_trace_direction(*matrix_iter);
    }

    /*!\brief Constructs from the underlying trace matrix iterator indicating the start of the trace path.
     * \tparam other_matrix_iter_t The underlying matrix iterator type of `other`; the condition
     *                             `std::constructible_from<matrix_iter_t, other_matrix_iter_t>` must evaluate to
     *                             `true`.
     * \param[in] other The underlying matrix iterator.
     *
     * \details
     *
     * Allows the conversion of non-const to const iterator.
     */
    template <typename other_derived_t, two_dimensional_matrix_iterator other_matrix_iter_t>
        requires std::constructible_from<matrix_iter_t, other_matrix_iter_t>
    constexpr trace_iterator_base(trace_iterator_base<other_derived_t, other_matrix_iter_t> const & other) noexcept :
        trace_iterator_base{other.matrix_iter}
    {}
    //!\}

public:
    /*!\name Associated types
     * \{
     */
    // Doxygen: https://github.com/seqan/product_backlog/issues/424
    //!\brief The value type.
    using value_type = trace_directions;
    using reference = trace_directions const &;          //!< The reference type.
    using pointer = value_type const *;                  //!< The pointer type.
    using difference_type = std::ptrdiff_t;              //!< The difference type.
    using iterator_category = std::forward_iterator_tag; //!< Forward iterator tag.
    //!\}

    /*!\name Element access
     * \{
     */
    //!\brief Returns the current trace direction.
    reference operator*() const noexcept
    {
        return current_direction;
    }

    //!\brief Returns a pointer to the current trace direction.
    pointer operator->() const noexcept
    {
        return &current_direction;
    }

    //!\brief Returns the current coordinate in two-dimensional space.
    [[nodiscard]] constexpr matrix_coordinate coordinate() const noexcept
    {
        return matrix_iter.coordinate();
    }
    //!\}

    /*!\name Arithmetic operators
     * \{
     */
    //!\brief Advances the iterator by one.
    constexpr derived_t & operator++() noexcept
    {
        trace_directions old_dir = *matrix_iter;

        assert(old_dir != trace_directions::none);

        if (current_direction == trace_directions::up)
        {
            derived().go_up(matrix_iter);
            // Set new trace direction if last position was up_open.
            if (static_cast<bool>(old_dir & trace_directions::carry_up_open))
                set_trace_direction(*matrix_iter);
        }
        else if (current_direction == trace_directions::left)
        {
            derived().go_left(matrix_iter);
            // Set new trace direction if last position was left_open.
            if (static_cast<bool>(old_dir & trace_directions::carry_left_open))
                set_trace_direction(*matrix_iter);
        }
        else
        {
            assert(current_direction == trace_directions::diagonal);

            derived().go_diagonal(matrix_iter);
            set_trace_direction(*matrix_iter);
        }
        return derived();
    }

    //!\brief Returns an iterator advanced by one.
    constexpr derived_t operator++(int) noexcept
    {
        derived_t tmp{derived()};
        ++(*this);
        return tmp;
    }
    //!\}

    /*!\name Comparison operators
     * \{
     */
    //!\brief Returns `true` if both iterators are equal, `false` otherwise.
    constexpr friend bool operator==(derived_t const & lhs, derived_t const & rhs) noexcept
    {
        return lhs.matrix_iter == rhs.matrix_iter;
    }

    //!\brief Returns `true` if the pointed-to-element is seqan3::detail::trace_directions::none.
    constexpr friend bool operator==(derived_t const & lhs, std::default_sentinel_t const &) noexcept
    {
        return *lhs.matrix_iter == trace_directions::none;
    }

    //!\brief copydoc operator==()
    constexpr friend bool operator==(std::default_sentinel_t const &, derived_t const & rhs) noexcept
    {
        return rhs == std::default_sentinel;
    }

    //!\brief Returns `true` if both iterators are not equal, `false` otherwise.
    constexpr friend bool operator!=(derived_t const & lhs, derived_t const & rhs) noexcept
    {
        return !(lhs == rhs);
    }

    //!\brief Returns `true` if the pointed-to-element is not seqan3::detail::trace_directions::none.
    constexpr friend bool operator!=(derived_t const & lhs, std::default_sentinel_t const &) noexcept
    {
        return !(lhs == std::default_sentinel);
    }

    //!\brief copydoc operator!=()
    constexpr friend bool operator!=(std::default_sentinel_t const &, derived_t const & rhs) noexcept
    {
        return !(rhs == std::default_sentinel);
    }
    //!\}

private:
    /*!\name Overload functions
     * \brief These functions can be overloaded by the derived class to customise the iterator.
     * \{
     */
    //!\brief Moves iterator to previous left cell.
    constexpr void go_left(matrix_iter_t & iter) const noexcept
    {
        iter -= matrix_offset{row_index_type{0}, column_index_type{1}};
    }

    //!\brief Moves iterator to previous up cell.
    constexpr void go_up(matrix_iter_t & iter) const noexcept
    {
        iter -= matrix_offset{row_index_type{1}, column_index_type{0}};
    }

    //!\brief Moves iterator to previous diagonal cell.
    constexpr void go_diagonal(matrix_iter_t & iter) const noexcept
    {
        iter -= matrix_offset{row_index_type{1}, column_index_type{1}};
    }
    //!\}

    //!\brief Updates the current trace direction.
    void set_trace_direction(trace_directions const dir) noexcept
    {
        if (static_cast<bool>(dir & trace_directions::diagonal))
        {
            current_direction = trace_directions::diagonal;
        }
        else if (static_cast<bool>(dir & trace_directions::up))
        {
            current_direction = trace_directions::up;
        }
        else if (static_cast<bool>(dir & trace_directions::left))
        {
            current_direction = trace_directions::left;
        }
        else
        {
            current_direction = trace_directions::none;
        }
    }

    //!\brief Cast this object to its derived type.
    constexpr derived_t & derived() noexcept
    {
        return static_cast<derived_t &>(*this);
    }

    //!\overload
    constexpr derived_t const & derived() const noexcept
    {
        return static_cast<derived_t const &>(*this);
    }

    matrix_iter_t matrix_iter{};          //!< The underlying matrix iterator.
    trace_directions current_direction{}; //!< The current trace direction.
};

} // namespace seqan3::detail
